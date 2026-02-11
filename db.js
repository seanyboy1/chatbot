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
  action: String, // 'connect', 'message', 'test_connection'
  message: String,
  response: String,
  timestamp: { type: Date, default: Date.now },
  sessionId: String,
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
