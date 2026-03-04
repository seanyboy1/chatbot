// Admin Dashboard Logic
import { initMatrixRain } from './matrix-rain.js';
import { initTerminalChat } from './shared-terminal.js';

// ══════════════════════════════════════════════════════════
// MATRIX RAIN ANIMATION
// ══════════════════════════════════════════════════════════

const rain = initMatrixRain('matrix-rain');
if (rain) {
  rain.start();
}

// ══════════════════════════════════════════════════════════
// DASHBOARD STATE
// ══════════════════════════════════════════════════════════

let messageCount = 0;
let startTime = Date.now();
let responseTimes = [];

function updateStats() {
  // Update message count
  const messageCountEl = document.getElementById('message-count');
  if (messageCountEl) {
    messageCountEl.textContent = messageCount;
  }

  // Update uptime
  const uptimeEl = document.getElementById('uptime');
  if (uptimeEl) {
    const uptime = Math.floor((Date.now() - startTime) / 1000 / 60);
    uptimeEl.textContent = `${uptime}m`;
  }

  // Update average response time
  const avgResponseEl = document.getElementById('avg-response');
  if (avgResponseEl && responseTimes.length > 0) {
    const avg = responseTimes.reduce((a, b) => a + b, 0) / responseTimes.length;
    avgResponseEl.textContent = `${avg.toFixed(1)}s`;
  }
}

function addActivityLog(message, type = 'info') {
  const log = document.getElementById('activity-log');
  if (!log) return;

  const item = document.createElement('div');
  item.className = 'activity-item';
  const time = new Date().toLocaleTimeString();
  item.textContent = `[${time}] ${message}`;
  log.prepend(item);

  // Keep only last 20 items
  while (log.children.length > 20) {
    log.removeChild(log.lastChild);
  }

  // Save to database
  fetch('/api/activity', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      message: `[${time}] ${message}`,
      action: type,
      context: 'admin'
    }),
  }).catch(error => console.error('Failed to save activity log:', error));
}

// ══════════════════════════════════════════════════════════
// TERMINAL CHAT INITIALIZATION
// ══════════════════════════════════════════════════════════

const terminal = initTerminalChat({
  chatWindowId: 'chat-window',
  userInputId: 'user-input',
  sendBtnId: 'send-btn',
  context: 'admin',
  onMessageSent: (text) => {
    // Track message sent
    messageCount++;
    const shortText = text.length > 30 ? text.substring(0, 30) + '...' : text;
    addActivityLog(`User: ${shortText}`, 'user');
  },
  onMessageReceived: (reply) => {
    // Track response received
    updateStats();
    addActivityLog('Response received', 'success');
  }
});

// Track response time
const originalSendMessage = terminal?.sendMessage;
if (terminal && originalSendMessage) {
  terminal.sendMessage = async function() {
    const sendTime = Date.now();

    try {
      await originalSendMessage.call(this);

      // Calculate response time
      const responseTime = (Date.now() - sendTime) / 1000;
      responseTimes.push(responseTime);
      if (responseTimes.length > 10) responseTimes.shift();
    } catch (error) {
      addActivityLog('ERROR: Connection failed', 'error');
    }
  };
}

// ══════════════════════════════════════════════════════════
// QUICK ACTIONS
// ══════════════════════════════════════════════════════════

document.getElementById('clear-chat')?.addEventListener('click', () => {
  if (confirm('Clear all chat messages?')) {
    const chatWindow = document.getElementById('chat-window');
    chatWindow.innerHTML = '';
    messageCount = 0;
    updateStats();
    addActivityLog('Chat history cleared', 'system');
  }
});

document.getElementById('export-chat')?.addEventListener('click', () => {
  const messages = Array.from(document.querySelectorAll('.message'))
    .map(m => m.textContent)
    .join('\n\n');

  const blob = new Blob([messages], { type: 'text/plain' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `chat-export-${Date.now()}.txt`;
  a.click();
  URL.revokeObjectURL(url);

  addActivityLog('Chat exported successfully', 'system');
});

document.getElementById('test-connection')?.addEventListener('click', async () => {
  addActivityLog('Testing webhook connection...', 'system');

  const webhookStatusEl = document.getElementById('webhook-status');

  try {
    const res = await fetch('/api/chat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        message: 'ping',
        context: 'admin'
      }),
    });

    if (res.ok) {
      addActivityLog('✓ Connection test: SUCCESS', 'success');
      if (webhookStatusEl) {
        webhookStatusEl.textContent = 'CONNECTED';
        webhookStatusEl.style.color = 'var(--green)';
      }
    } else {
      addActivityLog('✗ Connection test: FAILED (HTTP ' + res.status + ')', 'error');
      if (webhookStatusEl) {
        webhookStatusEl.textContent = 'ERROR';
        webhookStatusEl.style.color = '#ff5f56';
      }
    }
  } catch (error) {
    addActivityLog('✗ Connection test: FAILED', 'error');
    if (webhookStatusEl) {
      webhookStatusEl.textContent = 'OFFLINE';
      webhookStatusEl.style.color = '#ff5f56';
    }
  }
});

// ══════════════════════════════════════════════════════════
// ACTIVITY LOG LOADING
// ══════════════════════════════════════════════════════════

async function loadRecentActivity() {
  try {
    const res = await fetch('/api/activity');
    const data = await res.json();

    if (data.activity && data.activity.length > 0) {
      const activityLog = document.getElementById('activity-log');
      if (activityLog) {
        // Clear and rebuild from database (ensures persistence across sessions)
        activityLog.innerHTML = '';

        // Add recent activity from database (newest first)
        data.activity.forEach(activity => {
          const time = new Date(activity.timestamp).toLocaleTimeString();
          let message = '';

          if (activity.action === 'page_visit') {
            const userTypeLabel = activity.userType === 'admin' ? '👤' : activity.userType === 'customer' ? '💬' : '🌐';
            message = `${userTypeLabel} ${activity.userType || 'visitor'} from ${activity.ip}`;
          } else if (activity.action === 'connect') {
            const userTypeLabel = activity.userType === 'admin' ? '👤' : '💬';
            message = `📍 ${activity.userType || 'user'} connected: ${activity.ip}`;
          } else if (activity.action === 'message') {
            const userTypeLabel = activity.userType === 'admin' ? '👤' : '💬';
            message = `${userTypeLabel} Message from ${activity.ip}`;
          } else if (activity.message) {
            // Show custom activity messages (already has timestamp from database)
            message = activity.message.replace(/^\[\d{1,2}:\d{2}:\d{2}[^\]]*\]\s*/, ''); // Remove old timestamp
          }

          if (message) {
            const item = document.createElement('div');
            item.className = 'activity-item';
            item.textContent = `[${time}] ${message}`;
            activityLog.appendChild(item);
          }
        });

        // Add stats at bottom
        if (data.stats && data.stats.uniqueVisitors24h > 0) {
          const statsItem = document.createElement('div');
          statsItem.className = 'activity-item';
          statsItem.textContent = `📊 ${data.stats.uniqueVisitors24h} unique visitors (24h)`;
          activityLog.appendChild(statsItem);
        }
      }
    }
  } catch (error) {
    console.error('Failed to load activity:', error);
  }
}

// ══════════════════════════════════════════════════════════
// BOOT SEQUENCE
// ══════════════════════════════════════════════════════════

document.addEventListener('DOMContentLoaded', () => {
  const bootLines = document.querySelectorAll('.boot-sequence p');
  bootLines.forEach((line, i) => {
    line.style.opacity = '0';
    setTimeout(() => {
      line.style.opacity = '1';
    }, i * 400);
  });

  // Scroll chat window to bottom on load
  const chatWindow = document.getElementById('chat-window');
  const userInput = document.getElementById('user-input');

  setTimeout(() => {
    if (chatWindow) {
      chatWindow.scrollTop = chatWindow.scrollHeight;
    }
    if (userInput) {
      userInput.focus();
    }
  }, bootLines.length * 400);
});

// ══════════════════════════════════════════════════════════
// DASHBOARD INITIALIZATION
// ══════════════════════════════════════════════════════════

document.addEventListener('DOMContentLoaded', async () => {
  updateStats();
  setInterval(updateStats, 5000); // Update every 5 seconds

  // Load activity from database FIRST (this shows all previous sessions)
  await loadRecentActivity();

  // Then add current session logs
  addActivityLog('Dashboard initialized', 'system');

  // Get session ID (already handled by shared-terminal.js)
  if (terminal) {
    const sessionId = terminal.getSessionId();
    if (sessionId) {
      addActivityLog(`Session ID: ${sessionId.substring(0, 8)}...`, 'system');
    }
  }

  addActivityLog('System ready', 'system');

  // Refresh activity every 30 seconds
  setInterval(loadRecentActivity, 30000);
});

// ══════════════════════════════════════════════════════════
// MOBILE TAB NAVIGATION
// ══════════════════════════════════════════════════════════

document.addEventListener('DOMContentLoaded', () => {
  const tabBtns = document.querySelectorAll('.tab-btn');

  // Get individual cards
  const systemCard = document.querySelector('.stats-card:nth-child(1)'); // System Status
  const statsCard = document.querySelector('.stats-card:nth-child(2)');  // Statistics
  const actionsCard = document.querySelector('.actions-card');
  const activityCard = document.querySelector('.activity-card');
  const profileCard = document.querySelector('.profile-card');

  const allCards = [systemCard, statsCard, actionsCard, activityCard, profileCard];

  tabBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      tabBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      allCards.forEach(c => c?.classList.remove('active'));

      const tab = btn.getAttribute('data-tab');
      if (tab === 'system')     systemCard?.classList.add('active');
      else if (tab === 'statistics') statsCard?.classList.add('active');
      else if (tab === 'actions')    actionsCard?.classList.add('active');
      else if (tab === 'activity')   activityCard?.classList.add('active');
      else if (tab === 'profile') {
        profileCard?.classList.add('active');
        // Pre-fill username from localStorage
        import('/js/auth.js').then(({ getUser }) => {
          const u = getUser();
          const usernameEl = document.getElementById('ap-username');
          if (usernameEl && u) usernameEl.value = u.username || '';
        });
      }
    });
  });

  // Initialize: show system card by default
  systemCard?.classList.add('active');

  // ── Admin Profile Form ──────────────────────────────────────────────────
  const apSubmit = document.getElementById('ap-submit');
  if (apSubmit) {
    apSubmit.addEventListener('click', async () => {
      const username    = document.getElementById('ap-username').value.trim();
      const currentPassword = document.getElementById('ap-current-pw').value;
      const newPassword     = document.getElementById('ap-new-pw').value;
      const statusEl = document.getElementById('ap-status');

      if (!currentPassword) {
        statusEl.textContent = 'Current password required.';
        statusEl.style.color = '#FF4500';
        return;
      }

      apSubmit.disabled = true;
      apSubmit.textContent = 'SAVING...';
      statusEl.textContent = '';

      try {
        const { getToken } = await import('/js/auth.js');
        const res = await fetch('/api/admin/profile', {
          method: 'PUT',
          headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${getToken()}` },
          body: JSON.stringify({ username, currentPassword, newPassword }),
        });
        const data = await res.json();
        if (res.ok) {
          statusEl.textContent = 'Saved.';
          statusEl.style.color = '#27c93f';
          document.getElementById('ap-current-pw').value = '';
          document.getElementById('ap-new-pw').value = '';
        } else {
          statusEl.textContent = data.error || 'Update failed.';
          statusEl.style.color = '#FF4500';
        }
      } catch {
        statusEl.textContent = 'Connection error.';
        statusEl.style.color = '#FF4500';
      }
      apSubmit.disabled = false;
      apSubmit.textContent = 'SAVE CHANGES';
    });
  }
});
