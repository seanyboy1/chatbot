// ── Matrix rain effect ──
const canvas = document.getElementById('matrix-rain');
const ctx = canvas.getContext('2d');

function resizeCanvas() {
  canvas.width = window.innerWidth;
  canvas.height = window.innerHeight;
}
resizeCanvas();
window.addEventListener('resize', resizeCanvas);

const chars = 'アイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲン0123456789ABCDEF';
const fontSize = 14;
let columns = Math.floor(canvas.width / fontSize);
let drops = Array(columns).fill(1);

function drawRain() {
  ctx.fillStyle = 'rgba(10, 10, 10, 0.05)';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#40C4FF';
  ctx.font = `${fontSize}px monospace`;

  for (let i = 0; i < drops.length; i++) {
    const char = chars[Math.floor(Math.random() * chars.length)];
    ctx.fillText(char, i * fontSize, drops[i] * fontSize);

    if (drops[i] * fontSize > canvas.height && Math.random() > 0.975) {
      drops[i] = 0;
    }
    drops[i]++;
  }
}

setInterval(drawRain, 50);

window.addEventListener('resize', () => {
  columns = Math.floor(canvas.width / fontSize);
  drops = Array(columns).fill(1);
});

// ── Chat logic ──
const chatWindow = document.getElementById('chat-window');
const userInput = document.getElementById('user-input');
const sendBtn = document.getElementById('send-btn');

function appendMessage(text, type) {
  const msg = document.createElement('div');
  msg.classList.add('message', type);

  // Format the message with basic markdown-style parsing
  if (type === 'bot') {
    msg.innerHTML = formatBotMessage(text);
  } else {
    msg.textContent = text;
  }

  chatWindow.appendChild(msg);
  chatWindow.scrollTop = chatWindow.scrollHeight;
  return msg;
}

function formatBotMessage(text) {
  // Convert markdown-style formatting to HTML
  let formatted = text
    // Bold: **text** or __text__
    .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
    .replace(/__(.+?)__/g, '<strong>$1</strong>')
    // Code blocks: ```code```
    .replace(/```(.+?)```/gs, '<pre><code>$1</code></pre>')
    // Inline code: `code`
    .replace(/`(.+?)`/g, '<code>$1</code>')
    // Lists: - item or * item
    .replace(/^[\-\*] (.+)$/gm, '<li>$1</li>')
    // Numbered lists: 1. item
    .replace(/^\d+\.\s(.+)$/gm, '<li>$1</li>')
    // Headers: ## Header
    .replace(/^### (.+)$/gm, '<h3>$1</h3>')
    .replace(/^## (.+)$/gm, '<h2>$1</h2>')
    .replace(/^# (.+)$/gm, '<h1>$1</h1>')
    // Paragraphs: double newline
    .split('\n\n').map(para => {
      if (para.includes('<li>')) {
        return '<ul>' + para + '</ul>';
      }
      if (para.startsWith('<h') || para.startsWith('<pre>')) {
        return para;
      }
      return para ? '<p>' + para.replace(/\n/g, '<br>') + '</p>' : '';
    }).join('');

  return formatted;
}

function showTypingIndicator() {
  const msg = document.createElement('div');
  msg.classList.add('message', 'bot', 'typing');
  msg.textContent = '';
  chatWindow.appendChild(msg);
  chatWindow.scrollTop = chatWindow.scrollHeight;
  return msg;
}

async function sendMessage() {
  const text = userInput.value.trim();
  if (!text) return;

  appendMessage(text, 'user');
  userInput.value = '';
  userInput.disabled = true;
  sendBtn.disabled = true;

  const typing = showTypingIndicator();

  try {
    const res = await fetch('/api/chat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message: text }),
    });

    const data = await res.json();
    typing.remove();
    appendMessage(data.reply, 'bot');
  } catch {
    typing.remove();
    appendMessage('ERROR: CONNECTION LOST. RETRANSMIT.', 'bot');
  } finally {
    userInput.disabled = false;
    sendBtn.disabled = false;
    userInput.focus();
  }
}

sendBtn.addEventListener('click', sendMessage);
userInput.addEventListener('keydown', (e) => {
  if (e.key === 'Enter') sendMessage();
});

// Boot sequence animation
document.addEventListener('DOMContentLoaded', () => {
  const bootLines = document.querySelectorAll('.boot-sequence p');
  bootLines.forEach((line, i) => {
    line.style.opacity = '0';
    setTimeout(() => {
      line.style.opacity = '1';
    }, i * 400);
  });

  // Scroll chat window to bottom on load
  setTimeout(() => {
    chatWindow.scrollTop = chatWindow.scrollHeight;
    userInput.focus();
  }, bootLines.length * 400);
});

// ══════════════════════════════════════════════════════════
// DASHBOARD FUNCTIONALITY
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
}

// Override sendMessage to include dashboard tracking
const originalSendMessage = sendMessage;
sendMessage = async function() {
  const text = userInput.value.trim();
  if (!text) return;

  messageCount++;
  const sendTime = Date.now();

  // Log activity
  const shortText = text.length > 30 ? text.substring(0, 30) + '...' : text;
  addActivityLog(`User: ${shortText}`, 'user');

  // Call original sendMessage
  appendMessage(text, 'user');
  userInput.value = '';
  userInput.disabled = true;
  sendBtn.disabled = true;

  const typing = showTypingIndicator();

  try {
    const res = await fetch('/api/chat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message: text }),
    });

    const data = await res.json();
    typing.remove();
    appendMessage(data.reply, 'bot');

    // Track response time
    const responseTime = (Date.now() - sendTime) / 1000;
    responseTimes.push(responseTime);
    if (responseTimes.length > 10) responseTimes.shift();

    updateStats();
    addActivityLog('Response received', 'success');
  } catch {
    typing.remove();
    appendMessage('ERROR: CONNECTION LOST. RETRANSMIT.', 'bot');
    addActivityLog('ERROR: Connection failed', 'error');
  } finally {
    userInput.disabled = false;
    sendBtn.disabled = false;
    userInput.focus();
  }
};

// Quick Actions
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
      body: JSON.stringify({ message: 'ping' }),
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

// Initialize dashboard
document.addEventListener('DOMContentLoaded', () => {
  updateStats();
  setInterval(updateStats, 5000); // Update every 5 seconds
  addActivityLog('Dashboard initialized', 'system');
  addActivityLog('System ready', 'system');
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

  tabBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      // Remove active class from all buttons
      tabBtns.forEach(b => b.classList.remove('active'));

      // Add active class to clicked button
      btn.classList.add('active');

      // Hide all cards
      systemCard?.classList.remove('active');
      statsCard?.classList.remove('active');
      actionsCard?.classList.remove('active');
      activityCard?.classList.remove('active');

      // Show selected card
      const tab = btn.getAttribute('data-tab');
      if (tab === 'system') {
        systemCard?.classList.add('active');
      } else if (tab === 'statistics') {
        statsCard?.classList.add('active');
      } else if (tab === 'actions') {
        actionsCard?.classList.add('active');
      } else if (tab === 'activity') {
        activityCard?.classList.add('active');
      }
    });
  });

  // Initialize: show system card by default
  systemCard?.classList.add('active');
});
