import 'dotenv/config';
import express from 'express';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { connectDB, ActivityLog, Session, User, ChatSession, ServiceRequest, MeshNode, SikeNode, AdminSession } from './db.js';
import crypto from 'crypto';
import fs from 'fs';
import multer from 'multer';
import sharp from 'sharp';
import helmet from 'helmet';
import rateLimit from 'express-rate-limit';
import cors from 'cors';
import { parse as parseCookieLib } from 'cookie';

const __filename = fileURLToPath(import.meta.url);
const __dirname = process.env.ELECTRON_RESOURCE_DIR || dirname(__filename);

const app = express();
const PORT = process.env.PORT || 3000;

// IP geolocation helper (uses free ip-api.com, no key required)
async function geolocateIP(ip) {
  const clean = (ip || '').replace(/^::ffff:/, '').split(',')[0].trim();
  if (!clean || clean === '127.0.0.1' || clean.startsWith('192.168.') || clean.startsWith('10.') || clean.startsWith('::1')) {
    return 'Local';
  }
  try {
    const res = await fetch(`http://ip-api.com/json/${clean}?fields=status,city,regionName,country`);
    const d = await res.json();
    if (d.status === 'success') return [d.city, d.regionName, d.country].filter(Boolean).join(', ') || 'Unknown';
  } catch {}
  return 'Unknown';
}

// n8n webhook integration
const N8N_WEBHOOK_URL = process.env.N8N_WEBHOOK_URL;
if (!N8N_WEBHOOK_URL) {
  console.error('ERROR: N8N_WEBHOOK_URL environment variable is not set');
  console.error('Please create a .env file with N8N_WEBHOOK_URL or set it in your environment');
  process.exit(1);
}

// ── Security headers ─────────────────────────────────────────────────────────
app.use(helmet({
  contentSecurityPolicy: false, // disabled — inline scripts/styles used throughout
  crossOriginEmbedderPolicy: false,
}));

// ── CORS ─────────────────────────────────────────────────────────────────────
const allowedOrigins = (process.env.ALLOWED_ORIGINS || '').split(',').map(s => s.trim()).filter(Boolean);
app.use(cors({
  origin: (origin, cb) => {
    // Allow same-origin, non-browser, or explicitly whitelisted origins
    if (!origin || allowedOrigins.length === 0 || allowedOrigins.includes(origin)) return cb(null, true);
    cb(new Error('CORS not allowed'));
  },
  credentials: true,
}));

app.use(express.json({ limit: '50kb' }));

// Skip ngrok browser warning for all responses (allows API calls from mobile to pass through)
app.use((req, res, next) => {
  res.setHeader('ngrok-skip-browser-warning', '1');
  next();
});

// ── Text sanitiser (strips newlines/control chars — prevents SMS injection) ──
function sanitizeText(s) {
  return String(s || '').replace(/[\r\n\t]/g, ' ').trim().slice(0, 200);
}

// ── SMS via Twilio ───────────────────────────────────────────────────────────
async function sendSMS(body) {
  const sid  = process.env.TWILIO_ACCOUNT_SID;
  const auth = process.env.TWILIO_AUTH_TOKEN;
  const from = process.env.TWILIO_FROM_NUMBER;
  const to   = process.env.TWILIO_TO_NUMBER;
  if (!sid || !auth || !from || !to) return; // SMS not configured — skip silently
  try {
    const payload = new URLSearchParams({ To: to, From: from, Body: body });
    await fetch(`https://api.twilio.com/2010-04-01/Accounts/${sid}/Messages.json`, {
      method: 'POST',
      headers: {
        'Authorization': 'Basic ' + Buffer.from(`${sid}:${auth}`).toString('base64'),
        'Content-Type': 'application/x-www-form-urlencoded',
      },
      body: payload,
    });
  } catch (err) {
    console.error('SMS send error:', err.message);
  }
}

// ── Password utilities ──────────────────────────────────────────────────────
function hashPassword(password, salt) {
  return crypto.pbkdf2Sync(password, salt, 100000, 64, 'sha256').toString('hex');
}

// ── Cookie parser ─────────────────────────────────────────────────────────────
function parseCookies(cookieHeader) {
  try { return parseCookieLib(cookieHeader || ''); } catch { return {}; }
}

// ── Rate limiters ─────────────────────────────────────────────────────────────
const loginLimiter = rateLimit({
  windowMs: 15 * 60 * 1000,
  max: 10,
  message: { error: 'Too many login attempts. Try again in 15 minutes.' },
  standardHeaders: true,
  legacyHeaders: false,
});

const activityLimiter = rateLimit({
  windowMs: 60 * 1000,
  max: 60,
  message: { error: 'Too many requests.' },
  standardHeaders: true,
  legacyHeaders: false,
});

const uploadLimiter = rateLimit({
  windowMs: 60 * 1000,
  max: 20,
  message: { error: 'Too many uploads.' },
  standardHeaders: true,
  legacyHeaders: false,
});

// ── Admin session store (in-memory, with expiry) ─────────────────────────────
const adminSessions = new Map(); // token → expiresAt
const ADMIN_SESSION_TTL = 12 * 60 * 60 * 1000; // 12 hours

// ── Admin page guard (server-side, checks httpOnly cookie) ───────────────────
async function requireAdminPage(req, res, next) {
  const cookies = parseCookies(req.headers.cookie);
  const token = cookies.admin_session;
  if (!token) return res.redirect('/');

  // Check in-memory first (fast path)
  const expiry = adminSessions.get(token);
  if (expiry && Date.now() <= expiry) return next();

  // Fall back to MongoDB (survives server restarts)
  try {
    await connectDB();
    const dbSession = await AdminSession.findOne({ token });
    if (dbSession && dbSession.expiresAt > new Date()) {
      adminSessions.set(token, dbSession.expiresAt.getTime()); // warm in-memory cache
      return next();
    }
  } catch { /* if DB unavailable, fall through to deny */ }

  adminSessions.delete(token);
  res.setHeader('Set-Cookie', 'admin_session=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict');
  return res.redirect('/');
}

// ── Admin password: migrate plaintext → hashed on startup ───────────────────
function migrateAdminPassword() {
  const plain = process.env.ADMIN_PASSWORD;
  const existingHash = process.env.ADMIN_PASSWORD_HASH;
  if (plain && !existingHash) {
    const salt = crypto.randomBytes(16).toString('hex');
    const hash = hashPassword(plain, salt);
    process.env.ADMIN_PASSWORD_HASH = hash;
    process.env.ADMIN_PASSWORD_SALT = salt;
    delete process.env.ADMIN_PASSWORD;
    try {
      const envPath = join(__dirname, '.env');
      let content = fs.readFileSync(envPath, 'utf8');
      content = content.replace(
        /^ADMIN_PASSWORD=.*$/m,
        `ADMIN_PASSWORD_HASH=${hash}\nADMIN_PASSWORD_SALT=${salt}`
      );
      fs.writeFileSync(envPath, content, 'utf8');
      console.log('[security] Admin password migrated to hash.');
    } catch (err) {
      console.error('[security] Failed to persist admin password hash:', err.message);
    }
  }
}
migrateAdminPassword();

// ── Auth middleware ─────────────────────────────────────────────────────────
async function requireAuth(req, res, next) {
  const token = req.headers.authorization?.replace('Bearer ', '').trim();
  if (!token) return res.status(401).json({ error: 'Unauthorized' });

  // Check admin sessions first
  const adminExpiry = adminSessions.get(token);
  if (adminExpiry) {
    if (Date.now() > adminExpiry) {
      adminSessions.delete(token);
      return res.status(401).json({ error: 'Session expired. Please log in again.' });
    }
    req.isAdmin = true;
    req.user = { name: 'Administrator', role: 'admin' };
    return next();
  }

  try {
    await connectDB();
    const user = await User.findOne({ authToken: token });
    if (!user) return res.status(401).json({ error: 'Invalid token' });
    req.user = user;
    next();
  } catch {
    res.status(500).json({ error: 'Auth error' });
  }
}

// Middleware to log every page visit
app.use(async (req, res, next) => {
  // Only log HTML page requests (not CSS, JS, images, API calls)
  const pagePaths = ['/', '/home', '/admin', '/login', '/profile'];
  if (req.method === 'GET' && pagePaths.includes(req.path)) {
    const ip = req.headers['x-forwarded-for'] ||
               req.headers['x-real-ip'] ||
               req.connection.remoteAddress ||
               req.ip;
    const userAgent = req.headers['user-agent'];

    // Determine userType based on route
    let userType = 'customer'; // Main page is customer page
    if (req.path === '/home') userType = 'visitor';
    if (req.path === '/admin') userType = 'admin';

    // Log page visit in the background — don't block the page load
    connectDB().then(() => ActivityLog.create({
      ip,
      userAgent,
      action: 'page_visit',
      sessionId: 'pending',
      timestamp: new Date(),
      userType,
    })).catch(err => console.error('Page visit logging error:', err));
  }
  next();
});

// Page routes (before static middleware)
app.get('/', (req, res) => {
  res.sendFile(join(__dirname, 'public', 'restricted.html'));
});

app.get('/dashboard', requireAdminPage, (req, res) => {
  res.sendFile(join(__dirname, 'public', 'dashboard.html'));
});

app.get('/bluetip', requireAdminPage, (req, res) => {
  res.sendFile(join(__dirname, 'public', 'bluetip.html'));
});

app.get('/bluetip-home', (req, res) => {
  res.sendFile(join(__dirname, 'public', 'bluetip-home.html'));
});

app.get('/bluetip-contact', (req, res) => {
  res.sendFile(join(__dirname, 'public', 'bluetip-contact.html'));
});

app.get('/bluetip-customer', (req, res) => {
  res.sendFile(join(__dirname, 'public', 'bluetip-customer.html'));
});

app.get('/bluetip-profile', (req, res) => {
  res.sendFile(join(__dirname, 'public', 'bluetip-profile.html'));
});

app.get('/home', (req, res) => {
  res.sendFile(join(__dirname, 'public', 'home.html'));
});

app.get('/admin', requireAdminPage, (req, res) => {
  res.sendFile(join(__dirname, 'public', 'admin.html'));
});

app.get('/chat', (req, res) => {
  res.sendFile(join(__dirname, 'public', 'chat.html'));
});

app.get('/contact', (req, res) => {
  res.sendFile(join(__dirname, 'public', 'contact.html'));
});

app.get('/login', (req, res) => {
  res.sendFile(join(__dirname, 'public', 'login.html'));
});

app.get('/profile', (req, res) => {
  res.sendFile(join(__dirname, 'public', 'profile.html'));
});

app.get('/mesh', requireAdminPage, (req, res) => {
  res.sendFile(join(__dirname, 'public', 'mesh.html'));
});

// ── Node image upload ─────────────────────────────────────────────────────────
const uploadsDir = join(__dirname, 'public', 'uploads', 'nodes');
if (!fs.existsSync(uploadsDir)) fs.mkdirSync(uploadsDir, { recursive: true });

const nodeUpload = multer({
  storage: multer.memoryStorage(),
  limits: { fileSize: 10 * 1024 * 1024 },
});

app.post('/api/upload/node-image', uploadLimiter, requireAuth, nodeUpload.single('image'), async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  if (!req.file) return res.status(400).json({ error: 'No image uploaded.' });
  try {
    const filename = `${Date.now()}-${crypto.randomBytes(4).toString('hex')}.jpg`;
    await sharp(req.file.buffer).rotate().jpeg({ quality: 85 }).toFile(join(uploadsDir, filename));
    res.json({ url: `/uploads/nodes/${filename}` });
  } catch (err) {
    console.error('[upload] processing error');
    res.status(500).json({ error: 'Image processing failed.' });
  }
});

// ── Mesh Nodes ───────────────────────────────────────────────────────────────
app.get('/api/mesh/nodes', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  try {
    await connectDB();
    const nodes = await MeshNode.find().sort({ addedAt: 1 });
    res.json({ nodes });
  } catch (err) {
    res.status(500).json({ error: 'Failed to load nodes.' });
  }
});

app.post('/api/mesh/nodes', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  const { nodeId, name, lat, lon, desc, online, imageUrl } = req.body;
  if (!nodeId || !name || lat == null || lon == null) return res.status(400).json({ error: 'nodeId, name, lat, lon required.' });
  try {
    await connectDB();
    const node = await MeshNode.create({ nodeId, name, lat, lon, desc, imageUrl, online: online !== false });
    res.json({ node });
  } catch (err) {
    res.status(500).json({ error: 'Failed to save node.' });
  }
});

app.put('/api/mesh/nodes/:nodeId', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  const { name, desc, online, imageUrl } = req.body;
  try {
    await connectDB();
    const update = {};
    if (name     !== undefined) update.name     = name;
    if (desc     !== undefined) update.desc     = desc;
    if (online   !== undefined) update.online   = online;
    if (imageUrl !== undefined) update.imageUrl = imageUrl;
    const node = await MeshNode.findOneAndUpdate({ nodeId: req.params.nodeId }, update, { new: true });
    if (!node) return res.status(404).json({ error: 'Node not found.' });
    res.json({ node });
  } catch (err) {
    res.status(500).json({ error: 'Failed to update node.' });
  }
});

app.delete('/api/mesh/nodes/:nodeId', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  try {
    await connectDB();
    await MeshNode.findOneAndDelete({ nodeId: req.params.nodeId });
    res.json({ success: true });
  } catch (err) {
    res.status(500).json({ error: 'Failed to delete node.' });
  }
});

// ── Blue-SIKE Page ───────────────────────────────────────────────────────────
app.get('/bluesike', requireAdminPage, (req, res) => {
  res.sendFile(join(__dirname, 'public', 'bluesike.html'));
});

app.get('/can-reader', requireAdminPage, (req, res) => {
  res.sendFile(join(__dirname, 'public', 'can-reader.html'));
});

// ── Blue-SIKE Nodes ──────────────────────────────────────────────────────────
app.get('/api/bluesike/nodes', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  try {
    await connectDB();
    const nodes = await SikeNode.find().sort({ addedAt: 1 });
    res.json({ nodes });
  } catch (err) {
    res.status(500).json({ error: 'Failed to load nodes.' });
  }
});

app.post('/api/bluesike/nodes', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  const { nodeId, name, lat, lon, desc, online, imageUrl } = req.body;
  if (!nodeId || !name || lat == null || lon == null) return res.status(400).json({ error: 'nodeId, name, lat, lon required.' });
  try {
    await connectDB();
    const node = await SikeNode.create({ nodeId, name, lat, lon, desc, imageUrl, online: online !== false });
    res.json({ node });
  } catch (err) {
    res.status(500).json({ error: 'Failed to save node.' });
  }
});

app.put('/api/bluesike/nodes/:nodeId', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  const { name, desc, online, lat, lon, imageUrl } = req.body;
  try {
    await connectDB();
    const update = {};
    if (name     !== undefined) update.name     = name;
    if (desc     !== undefined) update.desc     = desc;
    if (online   !== undefined) update.online   = online;
    if (lat      !== undefined) update.lat      = lat;
    if (lon      !== undefined) update.lon      = lon;
    if (imageUrl !== undefined) update.imageUrl = imageUrl;
    const node = await SikeNode.findOneAndUpdate({ nodeId: req.params.nodeId }, update, { new: true });
    if (!node) return res.status(404).json({ error: 'Node not found.' });
    res.json({ node });
  } catch (err) {
    res.status(500).json({ error: 'Failed to update node.' });
  }
});

app.delete('/api/bluesike/nodes/:nodeId', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  try {
    await connectDB();
    await SikeNode.findOneAndDelete({ nodeId: req.params.nodeId });
    res.json({ success: true });
  } catch (err) {
    res.status(500).json({ error: 'Failed to delete node.' });
  }
});

// ── Auth: Register ──────────────────────────────────────────────────────────
app.post('/api/auth/register', async (req, res) => {
  const { name, email, username, phone, password, service } = req.body;
  if (!name || !email || !username || !password) {
    return res.status(400).json({ error: 'Name, email, username, and password are required.' });
  }
  if (password.length < 8) {
    return res.status(400).json({ error: 'Password must be at least 8 characters.' });
  }
  const validService = service === 'bluetip' ? 'bluetip' : 'bluenet';
  try {
    await connectDB();
    const exists = await User.findOne({ $or: [{ email: email.toLowerCase() }, { username: username.toLowerCase() }] });
    if (exists) {
      const field = exists.email === email.toLowerCase() ? 'email' : 'username';
      return res.status(409).json({ error: `That ${field} is already registered.` });
    }
    const salt = crypto.randomBytes(16).toString('hex');
    const passwordHash = hashPassword(password, salt);
    const authToken = crypto.randomBytes(32).toString('hex');
    const user = await User.create({ name, email, username, phone, passwordHash, salt, authToken, service: validService });
    res.json({ token: authToken, user: { id: user._id, name: user.name, email: user.email, username: user.username, phone: user.phone, createdAt: user.createdAt } });
  } catch (err) {
    console.error('Register error:', err);
    res.status(500).json({ error: 'Registration failed.' });
  }
});

// ── Auth: Login ─────────────────────────────────────────────────────────────
app.post('/api/auth/login', loginLimiter, async (req, res) => {

  const { identifier, password, service } = req.body;
  if (!identifier || !password) {
    return res.status(400).json({ error: 'Credentials required.' });
  }

  // Check admin credentials first
  const adminUser = process.env.ADMIN_USERNAME;
  const adminHash = process.env.ADMIN_PASSWORD_HASH;
  const adminSalt = process.env.ADMIN_PASSWORD_SALT;
  const adminPlain = process.env.ADMIN_PASSWORD; // fallback if not yet migrated
  if (adminUser && identifier === adminUser) {
    const valid = adminHash && adminSalt
      ? hashPassword(password, adminSalt) === adminHash
      : (adminPlain && password === adminPlain);
    if (valid) {
      const token = 'admin-' + crypto.randomBytes(24).toString('hex');
      const expiresAt = new Date(Date.now() + ADMIN_SESSION_TTL);
      adminSessions.set(token, expiresAt.getTime());
      // Persist to MongoDB so session survives server restarts
      connectDB().then(() => AdminSession.create({ token, expiresAt })).catch(() => {});
      const isSecure = req.secure || req.headers['x-forwarded-proto'] === 'https';
      const cookieFlags = `HttpOnly; SameSite=Strict; Path=/; Max-Age=${ADMIN_SESSION_TTL / 1000}${isSecure ? '; Secure' : ''}`;
      res.setHeader('Set-Cookie', `admin_session=${token}; ${cookieFlags}`);
      return res.json({ token, role: 'admin', user: { name: 'Administrator', username: adminUser } });
    }
  }

  try {
    await connectDB();
    const id = identifier.toLowerCase();
    const user = await User.findOne({ $or: [{ email: id }, { username: id }] });
    if (!user) return res.status(401).json({ error: 'Invalid credentials.' });

    const hash = hashPassword(password, user.salt);
    if (hash !== user.passwordHash) return res.status(401).json({ error: 'Invalid credentials.' });

    // Enforce service separation
    if (service && user.service !== service) {
      const label = service === 'bluetip' ? 'BLUE-TIP' : 'BLUE-NET';
      return res.status(403).json({ error: `No ${label} account found. Please register.` });
    }

    // Rotate auth token on each login
    const authToken = crypto.randomBytes(32).toString('hex');
    user.authToken = authToken;
    await user.save();

    // Log login with geolocation (non-blocking)
    const loginIP = req.headers['x-forwarded-for'] || req.headers['x-real-ip'] || req.connection?.remoteAddress || req.ip;
    geolocateIP(loginIP).then(city => {
      ActivityLog.create({
        ip: loginIP, action: 'login',
        message: `Login: ${user.username}`,
        userType: user.service === 'bluetip' ? 'bluetip' : 'customer',
        sessionId: authToken.substring(0, 8),
        city,
      }).catch(() => {});
    });

    res.json({
      token: authToken,
      role: 'user',
      user: { id: user._id, name: user.name, email: user.email, username: user.username, phone: user.phone, createdAt: user.createdAt },
    });
  } catch (err) {
    console.error('Login error:', err);
    res.status(500).json({ error: 'Login failed.' });
  }
});

// ── Auth: Reissue cookie (for already-logged-in admin page refreshes) ────────
app.post('/api/auth/reissue-cookie', requireAuth, (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  const token = req.headers.authorization?.replace('Bearer ', '').trim();
  const isSecure = req.secure || req.headers['x-forwarded-proto'] === 'https';
  const cookieFlags = `HttpOnly; SameSite=Strict; Path=/; Max-Age=${ADMIN_SESSION_TTL / 1000}${isSecure ? '; Secure' : ''}`;
  res.setHeader('Set-Cookie', `admin_session=${token}; ${cookieFlags}`);
  res.json({ success: true });
});

// ── Auth: Logout (clears httpOnly cookie + invalidates session) ──────────────
app.post('/api/auth/logout', (req, res) => {
  const cookies = parseCookies(req.headers.cookie);
  const token = cookies.admin_session;
  if (token) adminSessions.delete(token);
  res.setHeader('Set-Cookie', 'admin_session=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict');
  res.json({ success: true });
});

// ── Admin: Update Profile ────────────────────────────────────────────────────
app.put('/api/admin/profile', requireAuth, (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  const { username, currentPassword, newPassword } = req.body;
  if (!currentPassword) return res.status(400).json({ error: 'Current password required.' });

  // Verify current password against hash (or plaintext fallback)
  const storedHash  = process.env.ADMIN_PASSWORD_HASH;
  const storedSalt  = process.env.ADMIN_PASSWORD_SALT;
  const storedPlain = process.env.ADMIN_PASSWORD;
  const valid = storedHash && storedSalt
    ? hashPassword(currentPassword, storedSalt) === storedHash
    : (storedPlain && currentPassword === storedPlain);
  if (!valid) return res.status(401).json({ error: 'Current password is incorrect.' });

  const newUser = (username || process.env.ADMIN_USERNAME).trim();

  // Hash the new password (or re-hash current if not changing)
  const passToHash = (newPassword || '').trim() || currentPassword;
  const newSalt = crypto.randomBytes(16).toString('hex');
  const newHash = hashPassword(passToHash, newSalt);

  process.env.ADMIN_USERNAME     = newUser;
  process.env.ADMIN_PASSWORD_HASH = newHash;
  process.env.ADMIN_PASSWORD_SALT = newSalt;
  delete process.env.ADMIN_PASSWORD;

  try {
    const envPath = join(__dirname, '.env');
    let content = fs.readFileSync(envPath, 'utf8');
    content = content.replace(/^ADMIN_USERNAME=.*$/m, `ADMIN_USERNAME=${newUser}`);
    // Update or add hash/salt lines, remove any plaintext password line
    if (/^ADMIN_PASSWORD_HASH=.*$/m.test(content)) {
      content = content.replace(/^ADMIN_PASSWORD_HASH=.*$/m, `ADMIN_PASSWORD_HASH=${newHash}`);
      content = content.replace(/^ADMIN_PASSWORD_SALT=.*$/m, `ADMIN_PASSWORD_SALT=${newSalt}`);
    } else {
      content = content.replace(/^ADMIN_PASSWORD=.*$/m, `ADMIN_PASSWORD_HASH=${newHash}\nADMIN_PASSWORD_SALT=${newSalt}`);
    }
    content = content.replace(/^ADMIN_PASSWORD=.*\n?/m, '');
    fs.writeFileSync(envPath, content, 'utf8');
  } catch (err) {
    console.error('Failed to persist admin credentials:', err);
  }

  res.json({ success: true });
});

// ── Admin: Customer List ─────────────────────────────────────────────────────
app.get('/api/admin/customers', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  try {
    await connectDB();
    const users = await User.find({}).select('-passwordHash -salt -authToken').sort({ createdAt: -1 });
    const withCounts = await Promise.all(users.map(async u => {
      const chatCount = await ChatSession.countDocuments({ userId: u._id });
      return { id: u._id, name: u.name, email: u.email, username: u.username, phone: u.phone, createdAt: u.createdAt, chatCount };
    }));
    res.json({ customers: withCounts });
  } catch (err) {
    console.error('Admin customers error:', err);
    res.status(500).json({ error: 'Failed to load customers.' });
  }
});

// ── Admin: Customer Detail ────────────────────────────────────────────────────
app.get('/api/admin/customers/:id', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  try {
    await connectDB();
    const user = await User.findById(req.params.id).select('-passwordHash -salt -authToken');
    if (!user) return res.status(404).json({ error: 'Customer not found.' });
    const chats = await ChatSession.find({ userId: user._id }).sort({ lastMessageAt: -1 }).limit(10);
    const serviceRequests = await ServiceRequest.find({ userId: user._id }).sort({ createdAt: -1 }).limit(10);
    res.json({ customer: { id: user._id, name: user.name, email: user.email, username: user.username, phone: user.phone, createdAt: user.createdAt }, chats, serviceRequests });
  } catch (err) {
    res.status(500).json({ error: 'Failed to load customer.' });
  }
});

// ── Admin: Update Customer ────────────────────────────────────────────────────
app.put('/api/admin/customers/:id', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  const { name, email, phone, newPassword } = req.body;
  try {
    await connectDB();
    const user = await User.findById(req.params.id);
    if (!user) return res.status(404).json({ error: 'Customer not found.' });
    if (name)  user.name  = name;
    if (email) user.email = email.toLowerCase();
    if (phone !== undefined) user.phone = phone;
    if (newPassword) {
      user.salt = crypto.randomBytes(16).toString('hex');
      user.passwordHash = hashPassword(newPassword, user.salt);
    }
    await user.save();
    res.json({ customer: { id: user._id, name: user.name, email: user.email, username: user.username, phone: user.phone, createdAt: user.createdAt } });
  } catch (err) {
    res.status(500).json({ error: 'Failed to update customer.' });
  }
});

// ── Auth: Me ────────────────────────────────────────────────────────────────
app.get('/api/auth/me', requireAuth, async (req, res) => {
  if (req.isAdmin) return res.json({ role: 'admin', user: req.user });
  const u = req.user;
  res.json({ role: 'user', user: { id: u._id, name: u.name, email: u.email, username: u.username, phone: u.phone, createdAt: u.createdAt } });
});

// ── Admin: All Service Requests ──────────────────────────────────────────────
app.get('/api/admin/service-requests', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  try {
    await connectDB();
    const requests = await ServiceRequest.find().sort({ createdAt: -1 }).limit(100);
    res.json({ requests });
  } catch (err) {
    res.status(500).json({ error: 'Failed to load requests.' });
  }
});

// ── Admin: Update Service Request Status + Reply ──────────────────────────────
app.put('/api/admin/service-requests/:id', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  const { status, adminReply } = req.body;
  try {
    await connectDB();
    const update = {};
    if (status) update.status = status;
    if (adminReply !== undefined) { update.adminReply = adminReply; update.repliedAt = new Date(); }
    const updated = await ServiceRequest.findByIdAndUpdate(req.params.id, update, { new: true });
    if (!updated) return res.status(404).json({ error: 'Request not found.' });
    res.json({ request: updated });
  } catch (err) {
    res.status(500).json({ error: 'Failed to update request.' });
  }
});

// ── Auth: Update Profile ─────────────────────────────────────────────────────
app.put('/api/auth/profile', requireAuth, async (req, res) => {
  if (req.isAdmin) return res.status(403).json({ error: 'Not allowed for admin.' });
  const { name, email, phone, currentPassword, newPassword } = req.body;
  try {
    await connectDB();
    const user = req.user;

    if (email && email.toLowerCase() !== user.email) {
      const exists = await User.findOne({ email: email.toLowerCase(), _id: { $ne: user._id } });
      if (exists) return res.status(409).json({ error: 'That email is already in use.' });
      user.email = email.toLowerCase();
    }
    if (name) user.name = name;
    if (phone !== undefined) user.phone = phone;

    if (newPassword) {
      if (!currentPassword) return res.status(400).json({ error: 'Current password is required.' });
      const check = hashPassword(currentPassword, user.salt);
      if (check !== user.passwordHash) return res.status(401).json({ error: 'Current password is incorrect.' });
      user.salt = crypto.randomBytes(16).toString('hex');
      user.passwordHash = hashPassword(newPassword, user.salt);
    }

    await user.save();
    res.json({ user: { id: user._id, name: user.name, email: user.email, username: user.username, phone: user.phone, createdAt: user.createdAt } });
  } catch (err) {
    console.error('Profile update error:', err);
    res.status(500).json({ error: 'Update failed.' });
  }
});

// ── Chats: List sessions ────────────────────────────────────────────────────
app.get('/api/chats', requireAuth, async (req, res) => {
  if (req.isAdmin) return res.json({ sessions: [] });
  try {
    await connectDB();
    const sessions = await ChatSession.find({ userId: req.user._id })
      .sort({ lastMessageAt: -1 })
      .limit(20)
      .select('title startedAt lastMessageAt messages');
    res.json({ sessions });
  } catch (err) {
    res.status(500).json({ error: 'Could not fetch chats.' });
  }
});

// ── Chats: New session ──────────────────────────────────────────────────────
app.post('/api/chats', requireAuth, async (req, res) => {
  try {
    await connectDB();
    const session = await ChatSession.create({ userId: req.user._id });
    res.json({ sessionId: session._id });
  } catch (err) {
    res.status(500).json({ error: 'Could not create chat session.' });
  }
});

// ── Chats: Send message (saves to ChatSession + forwards to n8n) ────────────
app.post('/api/chats/:sessionId/message', requireAuth, async (req, res) => {
  const { message } = req.body;
  const { sessionId } = req.params;
  if (!message) return res.status(400).json({ error: 'Message required.' });

  try {
    await connectDB();
    const session = await ChatSession.findOne({ _id: sessionId, userId: req.user._id });
    if (!session) return res.status(404).json({ error: 'Session not found.' });

    // Auto-title from first message
    const isFirstMessage = session.messages.length === 0;
    if (isFirstMessage) {
      session.title = message.slice(0, 48) + (message.length > 48 ? '…' : '');
      sendSMS(`BLUE-NET: New chat started\nFrom: ${sanitizeText(req.user.name || req.user.username)}\n"${sanitizeText(message).slice(0, 100)}"`);
    }

    session.messages.push({ role: 'user', content: message });
    session.lastMessageAt = new Date();

    // Forward to n8n
    let reply = '[ERROR] Could not reach AI.';
    try {
      const webhookUrl = `${N8N_WEBHOOK_URL}?message=${encodeURIComponent(message)}&context=user`;
      const response = await fetch(webhookUrl, { method: 'GET', signal: AbortSignal.timeout(30000) });
      if (response.ok) {
        const text = await response.text();
        if (text) {
          try {
            const data = JSON.parse(text);
            reply = (Array.isArray(data) && data[0]?.text) ? data[0].text : (data.reply || data.message || data.text || text);
          } catch { reply = text; }
        }
      }
    } catch { /* n8n timeout or error */ }

    session.messages.push({ role: 'bot', content: reply });
    await session.save();

    res.json({ reply, sessionId: session._id });
  } catch (err) {
    console.error('Chat message error:', err);
    res.status(500).json({ error: 'Message failed.' });
  }
});

// ── Service Request ─────────────────────────────────────────────────────────
app.post('/api/service-request', requireAuth, async (req, res) => {
  const { type, details } = req.body;
  if (!type || !details) return res.status(400).json({ error: 'Type and details required.' });
  try {
    await connectDB();
    const user = req.isAdmin ? null : req.user;
    await ServiceRequest.create({
      userId: user?._id,
      name: user?.name,
      email: user?.email,
      type,
      details,
      phone: user?.phone,
    });
    sendSMS(`BLUE-NET: New service request\nType: ${sanitizeText(type).toUpperCase().replace('_',' ')}\nFrom: ${sanitizeText(user?.name || 'Guest')}\n${sanitizeText(details).slice(0, 100)}`);
    res.json({ success: true, message: 'Service request submitted successfully.' });
  } catch (err) {
    res.status(500).json({ error: 'Failed to submit request.' });
  }
});

// ── Get Service Requests for current user ───────────────────────────────────
app.get('/api/service-requests', requireAuth, async (req, res) => {
  if (req.isAdmin) return res.json({ requests: [] });
  try {
    await connectDB();
    const query = { $or: [{ userId: req.user._id }, { email: req.user.email }] };
    const requests = await ServiceRequest.find(query).sort({ createdAt: -1 }).limit(10);
    res.json({ requests });
  } catch (err) {
    res.status(500).json({ error: 'Failed to load requests.' });
  }
});

// ── Callback Request ────────────────────────────────────────────────────────
app.post('/api/callback-request', requireAuth, async (req, res) => {
  const { phone, preferredTime, details } = req.body;
  if (!phone) return res.status(400).json({ error: 'Phone number required.' });
  try {
    await connectDB();
    const user = req.isAdmin ? null : req.user;
    await ServiceRequest.create({
      userId: user?._id,
      name: user?.name,
      email: user?.email,
      type: 'callback',
      details: details || '',
      phone,
      preferredTime,
    });
    sendSMS(`BLUE-NET: Callback request\nFrom: ${sanitizeText(user?.name || 'Guest')}\nPhone: ${sanitizeText(phone)}${preferredTime ? '\nTime: ' + sanitizeText(preferredTime) : ''}`);
    res.json({ success: true, message: 'Callback request submitted. We will contact you shortly.' });
  } catch (err) {
    res.status(500).json({ error: 'Failed to submit callback request.' });
  }
});

app.use(express.static(join(__dirname, 'public')));

// Endpoint to save activity log entry
app.post('/api/activity', activityLimiter, async (req, res) => {
  const ip = req.headers['x-forwarded-for']?.split(',')[0].trim() || req.headers['x-real-ip'] || req.connection.remoteAddress || req.ip;
  const { message, action, context } = req.body;

  if (!message) {
    return res.status(400).json({ error: 'Message is required' });
  }

  const userAgent = req.headers['user-agent'];

  // Determine userType from context
  let userType = 'visitor';
  if (context === 'admin') userType = 'admin';
  if (context === 'customer') userType = 'customer';

  try {
    await connectDB();
    await ActivityLog.create({
      ip,
      userAgent,
      action: action || 'user_action',
      message,
      sessionId: req.body.sessionId || 'unknown',
      userType,
    });

    res.json({ success: true });
  } catch (error) {
    console.error('Activity logging error:', error);
    res.status(500).json({ error: 'Failed to log activity' });
  }
});

// Endpoint to get recent activity from database
app.get('/api/activity', requireAuth, async (req, res) => {
  if (!req.isAdmin) return res.status(403).json({ error: 'Admin only.' });
  try {
    await connectDB();

    // Optional filter by userType
    const { userType } = req.query;
    const filter = userType ? { userType } : {};

    // Get last 20 activity logs
    const recentActivity = await ActivityLog.find(filter)
      .sort({ timestamp: -1 })
      .limit(20)
      .select('ip action timestamp message userType city');

    // Get unique visitor count (last 24 hours)
    const oneDayAgo = new Date(Date.now() - 24 * 60 * 60 * 1000);
    const uniqueVisitors = await Session.countDocuments({
      lastSeen: { $gte: oneDayAgo }
    });

    res.json({
      activity: recentActivity,
      stats: {
        uniqueVisitors24h: uniqueVisitors
      }
    });
  } catch (error) {
    console.error('Activity fetch error:', error);
    res.json({ activity: [], stats: { uniqueVisitors24h: 0 } });
  }
});

// Endpoint to get user's IP address
app.get('/api/ip', activityLimiter, async (req, res) => {
  const ip = req.headers['x-forwarded-for']?.split(',')[0].trim() || req.headers['x-real-ip'] || req.connection.remoteAddress || req.ip;

  const userAgent = req.headers['user-agent'];
  const sessionId = crypto.randomBytes(16).toString('hex');
  const { context } = req.query; // Get context (admin/customer)

  // Determine userType from context
  let userType = 'visitor';
  if (context === 'admin') userType = 'admin';
  if (context === 'customer') userType = 'customer';

  // Log to database
  try {
    await connectDB();

    // Log connection activity
    await ActivityLog.create({
      ip,
      userAgent,
      action: 'connect',
      sessionId,
      userType,
    });

    // Create or update session
    await Session.findOneAndUpdate(
      { ip },
      {
        ip,
        userAgent,
        lastSeen: new Date(),
        sessionId,
        $inc: { messageCount: 0 },
        $setOnInsert: { firstSeen: new Date() },
      },
      { upsert: true, new: true }
    );
  } catch (error) {
    console.error('Database logging error:', error);
  }

  res.json({ ip, sessionId });
});

app.post('/api/chat', async (req, res) => {
  const { message, sessionId, context } = req.body;

  if (!message) {
    return res.status(400).json({ error: 'Message is required' });
  }

  // Get user info for logging
  const ip = req.headers['x-forwarded-for'] ||
             req.headers['x-real-ip'] ||
             req.connection.remoteAddress ||
             req.ip;
  const userAgent = req.headers['user-agent'];

  // Determine userType from context
  let userType = 'visitor';
  if (context === 'admin') userType = 'admin';
  if (context === 'customer') userType = 'customer';

  try {
    // Forward message to n8n webhook using GET request with query parameters
    let webhookUrl = `${N8N_WEBHOOK_URL}?message=${encodeURIComponent(message)}`;
    if (context) {
      webhookUrl += `&context=${encodeURIComponent(context)}`;
    }
    const response = await fetch(webhookUrl, {
      method: 'GET',
      signal: AbortSignal.timeout(30000), // 30 second timeout
    });

    if (!response.ok) {
      throw new Error(`n8n webhook returned ${response.status}`);
    }

    // Get response text first to handle empty responses
    const responseText = await response.text();

    let reply;
    if (!responseText || responseText.trim() === '') {
      reply = '[SYSTEM] n8n workflow executed but returned no response. Check your workflow output.';
    } else {
      try {
        const data = JSON.parse(responseText);

        // n8n returns array format: [{ "text": "response" }]
        if (Array.isArray(data) && data.length > 0 && data[0].text) {
          reply = data[0].text;
        } else {
          // Fallback to other common response formats
          reply = data.reply || data.message || data.output || data.text || JSON.stringify(data);
        }
      } catch {
        // If not JSON, use the raw text
        reply = responseText;
      }
    }

    // Log message to database
    try {
      await connectDB();
      await ActivityLog.create({
        ip,
        userAgent,
        action: 'message',
        message,
        response: reply,
        sessionId: sessionId || 'unknown',
        userType,
      });
      // SMS on first message from a guest/customer (sessionId absent = new session)
      if (!sessionId || sessionId === 'unknown') {
        sendSMS(`BLUE-NET: Guest chat message\nFrom: ${ip}\n"${message.slice(0, 100)}"`);
      }

      // Update session message count
      await Session.findOneAndUpdate(
        { ip },
        { $inc: { messageCount: 1 }, lastSeen: new Date() }
      );
    } catch (dbError) {
      console.error('Database logging error:', dbError);
    }

    res.json({ reply });
  } catch (error) {
    console.error('Error calling n8n webhook:', error);

    // Provide helpful error messages
    if (error.name === 'AbortError') {
      return res.status(504).json({
        reply: '[ERROR] Request timeout - n8n webhook took too long to respond.'
      });
    }

    if (error.cause?.code === 'ECONNREFUSED') {
      return res.status(503).json({
        reply: '[ERROR] Cannot connect to n8n. Make sure n8n is running on port 5678.'
      });
    }

    res.status(500).json({
      reply: `[ERROR] Failed to process message: ${error.message}`
    });
  }
});

app.listen(PORT, () => {
  console.log(`Server running on http://localhost:${PORT}`);
});
