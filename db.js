import mongoose from 'mongoose';

const MONGODB_URI = process.env.MONGODB_URI;

let isConnected = false;

export async function connectDB() {
  if (isConnected) {
    return;
  }

  if (!MONGODB_URI) {
    console.warn('MONGODB_URI not set - database logging disabled');
    return;
  }

  try {
    await mongoose.connect(MONGODB_URI);
    isConnected = true;
    console.log('MongoDB connected successfully');
  } catch (error) {
    console.error('MongoDB connection error:', error);
  }
}

// Activity Log Schema
const activityLogSchema = new mongoose.Schema({
  ip: String,
  userAgent: String,
  action: String, // 'connect', 'message', 'page_visit', 'test_connection'
  message: String,
  response: String,
  timestamp: { type: Date, default: Date.now },
  sessionId: String,
  userType: String, // 'admin', 'customer', 'visitor'
});

export const ActivityLog = mongoose.models.ActivityLog || mongoose.model('ActivityLog', activityLogSchema);

// Session Schema (track unique users)
const sessionSchema = new mongoose.Schema({
  ip: String,
  userAgent: String,
  firstSeen: { type: Date, default: Date.now },
  lastSeen: { type: Date, default: Date.now },
  messageCount: { type: Number, default: 0 },
  sessionId: { type: String, unique: true },
});

export const Session = mongoose.models.Session || mongoose.model('Session', sessionSchema);

// User Schema (registered accounts)
const userSchema = new mongoose.Schema({
  name: String,
  email: { type: String, unique: true, lowercase: true, trim: true },
  username: { type: String, unique: true, lowercase: true, trim: true },
  phone: String,
  passwordHash: String,
  salt: String,
  authToken: String,
  service: { type: String, enum: ['bluenet', 'bluetip'], default: 'bluenet' },
  createdAt: { type: Date, default: Date.now },
});
export const User = mongoose.models.User || mongoose.model('User', userSchema);

// Chat Session Schema (per-user saved conversations)
const chatSessionSchema = new mongoose.Schema({
  userId: { type: mongoose.Schema.Types.ObjectId, ref: 'User', required: true },
  title: { type: String, default: 'New Chat' },
  messages: [{
    role: { type: String, enum: ['user', 'bot'] },
    content: String,
    timestamp: { type: Date, default: Date.now },
  }],
  startedAt: { type: Date, default: Date.now },
  lastMessageAt: { type: Date, default: Date.now },
});
export const ChatSession = mongoose.models.ChatSession || mongoose.model('ChatSession', chatSessionSchema);

// Service Request Schema
const serviceRequestSchema = new mongoose.Schema({
  userId: { type: mongoose.Schema.Types.ObjectId, ref: 'User' },
  name: String,
  email: String,
  type: String, // 'new_install', 'upgrade', 'support', 'callback', 'other'
  details: String,
  phone: String,
  preferredTime: String,
  status: { type: String, default: 'pending' },
  adminReply: String,
  repliedAt: Date,
  createdAt: { type: Date, default: Date.now },
});
export const ServiceRequest = mongoose.models.ServiceRequest || mongoose.model('ServiceRequest', serviceRequestSchema);

// Mesh Node Schema (Meshtastic radio node locations)
const meshNodeSchema = new mongoose.Schema({
  nodeId: { type: String, required: true, unique: true },
  name: { type: String, required: true },
  lat: { type: Number, required: true },
  lon: { type: Number, required: true },
  desc: String,
  online: { type: Boolean, default: true },
  addedAt: { type: Date, default: Date.now },
});
export const MeshNode = mongoose.models.MeshNode || mongoose.model('MeshNode', meshNodeSchema);
