// ── BLUE-NET Auth Utilities ──────────────────────────────────────────────────
const TOKEN_KEY   = 'bluenet_token';
const USER_KEY    = 'bluenet_user';
const ROLE_KEY    = 'bluenet_role';
const SERVICE_KEY = 'bluenet_service';

export function saveAuth(token, user, role, service) {
  localStorage.setItem(TOKEN_KEY, token);
  localStorage.setItem(USER_KEY, JSON.stringify(user));
  localStorage.setItem(ROLE_KEY, role || 'user');
  if (service) localStorage.setItem(SERVICE_KEY, service);
}

export function getToken()   { return localStorage.getItem(TOKEN_KEY); }
export function getRole()    { return localStorage.getItem(ROLE_KEY) || 'user'; }
export function getService() { return localStorage.getItem(SERVICE_KEY) || 'bluenet'; }
export function getUser() {
  try { return JSON.parse(localStorage.getItem(USER_KEY)); } catch { return null; }
}

export function clearAuth() {
  localStorage.removeItem(TOKEN_KEY);
  localStorage.removeItem(USER_KEY);
  localStorage.removeItem(ROLE_KEY);
  localStorage.removeItem(SERVICE_KEY);
}

export function isLoggedIn() { return !!getToken(); }

/** Redirect to /login if not authenticated. Returns false if redirecting. */
export function requireAuth() {
  if (!getToken()) { window.location.href = '/login'; return false; }
  return true;
}

/** fetch() wrapper that injects Authorization header + JSON content-type */
export async function authFetch(url, options = {}) {
  const token = getToken();
  return fetch(url, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...(token ? { 'Authorization': `Bearer ${token}` } : {}),
      ...(options.headers || {}),
    },
  });
}
