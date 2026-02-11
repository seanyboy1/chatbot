# Database Logging Setup

Your Blue-Net chatbot now includes MongoDB database logging to track:
- User connections (IP addresses, timestamps)
- Chat messages and responses
- Session information
- Activity metrics

## What Gets Logged

### ActivityLog Collection
- IP address
- User agent (browser/device info)
- Action type (connect, message, test_connection)
- Message content
- Bot response
- Timestamp
- Session ID

### Session Collection
- IP address
- User agent
- First seen timestamp
- Last seen timestamp
- Message count per session
- Unique session ID

## Setup Instructions

### 1. Create Free MongoDB Atlas Account

1. Go to https://www.mongodb.com/cloud/atlas
2. Click "Try Free"
3. Create an account (free tier is sufficient)

### 2. Create a Cluster

1. After signing up, click "Build a Database"
2. Choose **FREE** tier (M0 Sandbox)
3. Select a cloud provider and region (choose one closest to your users)
4. Click "Create Cluster"

### 3. Create Database User

1. In the Security section, click "Database Access"
2. Click "Add New Database User"
3. Choose "Password" authentication
4. **Username**: `bluenet` (or your choice)
5. **Password**: Generate a strong password (save it!)
6. User Privileges: "Read and write to any database"
7. Click "Add User"

### 4. Configure Network Access

1. In the Security section, click "Network Access"
2. Click "Add IP Address"
3. Click "Allow Access from Anywhere" (0.0.0.0/0)
   - This is needed for Vercel serverless functions
4. Click "Confirm"

### 5. Get Connection String

1. Go back to "Database" section
2. Click "Connect" on your cluster
3. Choose "Connect your application"
4. Driver: **Node.js**
5. Copy the connection string - it looks like:
   ```
   mongodb+srv://bluenet:<password>@cluster0.xxxxx.mongodb.net/?retryWrites=true&w=majority
   ```

### 6. Configure Environment Variables

#### For Local Development:

Add to your `.env` file:
```bash
MONGODB_URI=mongodb+srv://bluenet:YOUR_PASSWORD_HERE@cluster0.xxxxx.mongodb.net/bluenet?retryWrites=true&w=majority
```

**Important:** Replace:
- `YOUR_PASSWORD_HERE` with your actual database password
- `cluster0.xxxxx` with your actual cluster address
- Added `/bluenet` database name before the `?`

#### For Vercel Production:

1. Go to https://vercel.com/dashboard
2. Click on your "chatbot" project
3. Go to **Settings** → **Environment Variables**
4. Add new variable:
   - **Name**: `MONGODB_URI`
   - **Value**: `mongodb+srv://bluenet:YOUR_PASSWORD_HERE@cluster0.xxxxx.mongodb.net/bluenet?retryWrites=true&w=majority`
   - Check: Production, Preview, Development
5. Click "Save"
6. Redeploy your project

## Viewing Your Data

### Using MongoDB Atlas Dashboard

1. Go to your MongoDB Atlas dashboard
2. Click "Browse Collections" on your cluster
3. You'll see two collections:
   - **activitylogs**: All user activity (connections, messages)
   - **sessions**: Unique user sessions

### Example Queries

You can use the MongoDB Atlas dashboard to:
- See all connections in the last hour
- View messages sent by a specific IP
- Track total message count per user
- Analyze peak usage times

## Privacy & Compliance

**Important:** You are now storing:
- IP addresses (personal data in some jurisdictions)
- User messages
- Usage patterns

**Recommendations:**
1. Add a privacy policy to your chatbot
2. Inform users their data is being logged
3. Consider data retention policies (auto-delete old logs)
4. Follow GDPR/CCPA requirements if applicable

## Optional: Data Retention Policy

Add this to `server.js` to auto-delete logs older than 30 days:

```javascript
// Run cleanup daily
setInterval(async () => {
  try {
    await connectDB();
    const thirtyDaysAgo = new Date(Date.now() - 30 * 24 * 60 * 60 * 1000);
    await ActivityLog.deleteMany({ timestamp: { $lt: thirtyDaysAgo } });
    console.log('Cleaned up old activity logs');
  } catch (error) {
    console.error('Cleanup error:', error);
  }
}, 24 * 60 * 60 * 1000); // Every 24 hours
```

## Troubleshooting

### Error: "MONGODB_URI not set"
- Database logging is optional and will be skipped if not configured
- Add the MONGODB_URI environment variable to enable logging

### Connection Errors
- Check your MongoDB Atlas "Network Access" allows 0.0.0.0/0
- Verify your password is correct in the connection string
- Make sure you replaced `<password>` with your actual password

### No Data Appearing
- Check Vercel logs for database errors
- Verify the environment variable is set in Vercel
- Redeploy after adding the environment variable

## Cost

MongoDB Atlas free tier includes:
- 512 MB storage (plenty for logging)
- Shared RAM
- No credit card required
- Perfect for this use case

You won't need to upgrade unless you have thousands of users per day.
