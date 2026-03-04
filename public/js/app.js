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
  const customersCard = document.querySelector('.customers-card');
  const requestsCard = document.querySelector('.requests-card');

  const dashMain = document.querySelector('.dashboard-main');
  const allCards = [systemCard, statsCard, actionsCard, activityCard, profileCard, customersCard, requestsCard];

  tabBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      tabBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      allCards.forEach(c => c?.classList.remove('active'));
      dashMain?.classList.remove('active');

      const tab = btn.getAttribute('data-tab');
      if (tab === 'system')     systemCard?.classList.add('active');
      else if (tab === 'statistics') statsCard?.classList.add('active');
      else if (tab === 'actions')    actionsCard?.classList.add('active');
      else if (tab === 'activity')   activityCard?.classList.add('active');
      else if (tab === 'chat')       dashMain?.classList.add('active');
      else if (tab === 'customers') {
        customersCard?.classList.add('active');
        loadCustomerList();
      }
      else if (tab === 'requests') {
        requestsCard?.classList.add('active');
        loadServiceRequests();
      }
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

  // ── Customers Tab ────────────────────────────────────────────────────────────
  let currentCustomerId = null;

  async function adminFetch(url, options = {}) {
    const { getToken } = await import('/js/auth.js');
    return fetch(url, {
      ...options,
      headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${getToken()}`, ...(options.headers || {}) },
    });
  }

  async function loadCustomerList() {
    const listEl = document.getElementById('customers-list');
    const detailEl = document.getElementById('customer-detail');
    if (!listEl) return;
    detailEl.style.display = 'none';
    listEl.style.display = 'block';
    listEl.innerHTML = '<div class="customers-loading">LOADING...</div>';

    try {
      const res = await adminFetch('/api/admin/customers');
      const data = await res.json();
      if (!res.ok) { listEl.innerHTML = `<div class="customers-loading">${data.error}</div>`; return; }

      const customers = data.customers || [];
      if (!customers.length) { listEl.innerHTML = '<div class="customers-loading">NO CUSTOMERS YET</div>'; return; }

      listEl.innerHTML = '';
      customers.forEach(c => {
        const row = document.createElement('div');
        row.className = 'customer-row';
        row.innerHTML = `
          <div class="customer-row-name">${escHtml(c.name || c.username)}</div>
          <div class="customer-row-meta">${escHtml(c.email)} &nbsp;·&nbsp; ${c.chatCount} chat${c.chatCount !== 1 ? 's' : ''}</div>
        `;
        row.addEventListener('click', () => loadCustomerDetail(c.id, c.name || c.username));
        listEl.appendChild(row);
      });
    } catch {
      listEl.innerHTML = '<div class="customers-loading">CONNECTION ERROR</div>';
    }
  }

  async function loadCustomerDetail(id, name) {
    currentCustomerId = id;
    const listEl = document.getElementById('customers-list');
    const detailEl = document.getElementById('customer-detail');
    listEl.style.display = 'none';
    detailEl.style.display = 'block';
    document.getElementById('customer-detail-name').textContent = name.toUpperCase();
    document.getElementById('customer-detail-fields').innerHTML = '<div class="customers-loading">LOADING...</div>';
    document.getElementById('customer-edit-form').style.display = 'none';
    document.getElementById('customer-chats-section').innerHTML = '';

    try {
      const res = await adminFetch(`/api/admin/customers/${id}`);
      const data = await res.json();
      if (!res.ok) { document.getElementById('customer-detail-fields').innerHTML = `<div class="customers-loading">${data.error}</div>`; return; }

      const c = data.customer;
      const since = c.createdAt ? new Date(c.createdAt).toLocaleDateString('en-US', { year: 'numeric', month: 'short', day: 'numeric' }) : '—';

      document.getElementById('customer-detail-fields').innerHTML = `
        <div class="customer-field-row"><span class="customer-field-label">USERNAME</span><span class="customer-field-value">${escHtml(c.username || '—')}</span></div>
        <div class="customer-field-row"><span class="customer-field-label">FULL NAME</span><span class="customer-field-value" id="cf-name">${escHtml(c.name || '—')}</span></div>
        <div class="customer-field-row"><span class="customer-field-label">EMAIL</span><span class="customer-field-value" id="cf-email">${escHtml(c.email || '—')}</span></div>
        <div class="customer-field-row"><span class="customer-field-label">PHONE</span><span class="customer-field-value" id="cf-phone">${escHtml(c.phone || '—')}</span></div>
        <div class="customer-field-row"><span class="customer-field-label">MEMBER SINCE</span><span class="customer-field-value">${since}</span></div>
        <button class="action-btn" id="ce-edit-btn" style="margin-top:10px;">EDIT ACCOUNT</button>
      `;

      document.getElementById('ce-edit-btn').addEventListener('click', () => {
        document.getElementById('ce-name').value  = c.name  || '';
        document.getElementById('ce-email').value = c.email || '';
        document.getElementById('ce-phone').value = c.phone || '';
        document.getElementById('ce-status').textContent = '';
        document.getElementById('customer-edit-form').style.display = 'block';
        document.getElementById('ce-edit-btn').style.display = 'none';
      });

      // Chat history
      const chats = data.chats || [];
      const chatsHtml = chats.length
        ? chats.map(s => {
            const d = new Date(s.lastMessageAt).toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' });
            const msgCount = s.messages?.length || 0;
            return `<div class="customer-chat-row">${escHtml(s.title || 'Chat Session')} <span class="customer-chat-meta">${d} · ${msgCount} msg${msgCount !== 1 ? 's' : ''}</span></div>`;
          }).join('')
        : '<div class="customers-loading" style="font-size:11px;">No chats yet</div>';

      document.getElementById('customer-chats-section').innerHTML = `
        <div class="customer-section-label">CHAT HISTORY</div>
        ${chatsHtml}
      `;
    } catch {
      document.getElementById('customer-detail-fields').innerHTML = '<div class="customers-loading">CONNECTION ERROR</div>';
    }
  }

  document.getElementById('customer-back')?.addEventListener('click', loadCustomerList);

  document.getElementById('ce-save')?.addEventListener('click', async () => {
    const name      = document.getElementById('ce-name').value.trim();
    const email     = document.getElementById('ce-email').value.trim();
    const phone     = document.getElementById('ce-phone').value.trim();
    const newPassword = document.getElementById('ce-new-pw')?.value || '';
    const statusEl  = document.getElementById('ce-status');
    const btn       = document.getElementById('ce-save');

    btn.disabled = true; btn.textContent = 'SAVING...';
    try {
      const body = { name, email, phone };
      if (newPassword) body.newPassword = newPassword;
      const res = await adminFetch(`/api/admin/customers/${currentCustomerId}`, {
        method: 'PUT', body: JSON.stringify(body),
      });
      const data = await res.json();
      if (res.ok) {
        document.getElementById('cf-name').textContent  = data.customer.name  || '—';
        document.getElementById('cf-email').textContent = data.customer.email || '—';
        document.getElementById('cf-phone').textContent = data.customer.phone || '—';
        if (document.getElementById('ce-new-pw')) document.getElementById('ce-new-pw').value = '';
        statusEl.textContent = 'Saved.'; statusEl.style.color = '#27c93f';
        setTimeout(() => {
          document.getElementById('customer-edit-form').style.display = 'none';
          document.getElementById('ce-edit-btn').style.display = '';
          statusEl.textContent = '';
        }, 1200);
      } else {
        statusEl.textContent = data.error || 'Update failed.'; statusEl.style.color = '#FF4500';
      }
    } catch {
      statusEl.textContent = 'Connection error.'; statusEl.style.color = '#FF4500';
    }
    btn.disabled = false; btn.textContent = 'SAVE';
  });

  document.getElementById('ce-cancel')?.addEventListener('click', () => {
    document.getElementById('customer-edit-form').style.display = 'none';
    document.getElementById('ce-edit-btn').style.display = '';
  });

  // ── Service Requests Tab ──────────────────────────────────────────────────
  const typeLabels = { new_install: 'NEW INSTALL', upgrade: 'UPGRADE', support: 'SUPPORT', callback: 'CALLBACK', other: 'OTHER' };
  const statusOptions = ['pending', 'in_progress', 'resolved', 'cancelled'];
  const statusColors  = { pending: '#FFD700', in_progress: '#FF8C00', resolved: '#27c93f', cancelled: 'var(--blue-dim)' };

  let allRequests = [];
  let activeFilter = 'all';

  async function loadServiceRequests() {
    const listEl = document.getElementById('requests-list');
    if (!listEl) return;
    listEl.innerHTML = '<div class="customers-loading">LOADING...</div>';

    try {
      const res = await adminFetch('/api/admin/service-requests');
      const data = await res.json();
      if (!res.ok) {
        if (res.status === 401 || res.status === 403) {
          listEl.innerHTML = '<div class="customers-loading">SESSION EXPIRED — SIGN OUT AND BACK IN</div>';
        } else {
          listEl.innerHTML = `<div class="customers-loading">${data.error || 'SERVER ERROR'}</div>`;
        }
        return;
      }
      allRequests = data.requests || [];
      renderRequests();
    } catch (err) {
      listEl.innerHTML = `<div class="customers-loading">CONNECTION ERROR: ${err.message}</div>`;
    }
  }

  let openRequestId = null;

  function renderRequests() {
    const listEl = document.getElementById('requests-list');
    if (!listEl) return;

    const filtered = activeFilter === 'all' ? allRequests : allRequests.filter(r => r.status === activeFilter);

    if (!filtered.length) {
      listEl.innerHTML = '<div class="customers-loading">NO REQUESTS</div>';
      return;
    }

    // Detail view
    if (openRequestId) {
      const r = allRequests.find(x => x._id === openRequestId);
      if (r) { renderRequestDetail(r, listEl); return; }
    }

    // List view
    listEl.innerHTML = '';
    filtered.forEach(r => {
      const date = new Date(r.createdAt).toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' });
      const typeLabel = typeLabels[r.type] || (r.type || '').toUpperCase();
      const statusColor = statusColors[r.status] || 'var(--blue-dim)';

      const item = document.createElement('div');
      item.className = 'customer-row';
      item.style.cssText = 'padding:10px 0;border-bottom:1px solid rgba(93,173,226,0.1);cursor:pointer;';
      item.innerHTML = `
        <div style="display:flex;align-items:center;justify-content:space-between;gap:8px;flex-wrap:wrap;">
          <div style="display:flex;align-items:center;gap:8px;">
            <span style="font-family:'Share Tech Mono',monospace;font-size:10px;letter-spacing:2px;color:var(--blue);background:rgba(93,173,226,0.08);border:1px solid var(--blue-dark);padding:2px 6px;border-radius:2px;">${typeLabel}</span>
            ${r.name ? `<span style="font-family:'Share Tech Mono',monospace;font-size:11px;color:var(--blue-dim);">${escHtml(r.name)}</span>` : ''}
          </div>
          <div style="display:flex;align-items:center;gap:10px;">
            <span style="font-family:'Share Tech Mono',monospace;font-size:10px;color:${statusColor};">● ${(r.status||'pending').toUpperCase().replace('_',' ')}</span>
            <span style="font-family:'Share Tech Mono',monospace;font-size:10px;color:var(--blue-dark);">${date}</span>
            <span style="color:var(--blue-dim);font-size:12px;">›</span>
          </div>
        </div>
        ${r.details ? `<div style="font-family:'Share Tech Mono',monospace;font-size:11px;color:var(--blue-dark);margin-top:4px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${escHtml(r.details.slice(0,80))}${r.details.length>80?'…':''}</div>` : ''}
      `;
      item.addEventListener('click', () => { openRequestId = r._id; renderRequests(); });
      listEl.appendChild(item);
    });
  }

  function renderRequestDetail(r, listEl) {
    const date = new Date(r.createdAt).toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' });
    const typeLabel = typeLabels[r.type] || (r.type || '').toUpperCase();
    const statusColor = statusColors[r.status] || 'var(--blue-dim)';
    const repliedDate = r.repliedAt ? new Date(r.repliedAt).toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' }) : null;

    listEl.innerHTML = `
      <div style="margin-bottom:14px;">
        <button id="req-back-btn" style="background:transparent;border:none;color:var(--blue-dim);font-family:'Share Tech Mono',monospace;font-size:11px;cursor:pointer;letter-spacing:1px;padding:0;">← BACK</button>
      </div>
      <div style="display:flex;align-items:center;gap:10px;margin-bottom:14px;flex-wrap:wrap;">
        <span style="font-family:'Share Tech Mono',monospace;font-size:10px;letter-spacing:2px;color:var(--blue);background:rgba(93,173,226,0.08);border:1px solid var(--blue-dark);padding:2px 8px;border-radius:2px;">${typeLabel}</span>
        <span style="font-family:'Share Tech Mono',monospace;font-size:10px;color:${statusColor};">● ${(r.status||'pending').toUpperCase().replace('_',' ')}</span>
        <span style="font-family:'Share Tech Mono',monospace;font-size:10px;color:var(--blue-dark);margin-left:auto;">${date}</span>
      </div>
      ${r.name || r.email ? `
      <div style="margin-bottom:10px;">
        ${r.name  ? `<div style="font-family:'Share Tech Mono',monospace;font-size:11px;color:var(--blue-dim);margin-bottom:2px;">NAME: ${escHtml(r.name)}</div>` : ''}
        ${r.email ? `<div style="font-family:'Share Tech Mono',monospace;font-size:11px;color:var(--blue-dim);margin-bottom:2px;">EMAIL: ${escHtml(r.email)}</div>` : ''}
        ${r.phone ? `<div style="font-family:'Share Tech Mono',monospace;font-size:11px;color:var(--blue-dim);margin-bottom:2px;">PHONE: ${escHtml(r.phone)}${r.preferredTime?' · '+escHtml(r.preferredTime):''}</div>` : ''}
      </div>` : ''}
      ${r.details ? `
      <div style="border:1px solid var(--blue-dark);border-radius:2px;padding:10px;margin-bottom:14px;background:rgba(0,0,0,0.2);">
        <div style="font-family:'Share Tech Mono',monospace;font-size:9px;letter-spacing:2px;color:var(--blue-dark);margin-bottom:6px;">REQUEST DETAILS</div>
        <div style="font-family:'Share Tech Mono',monospace;font-size:12px;color:var(--blue-dim);white-space:pre-wrap;word-break:break-word;">${escHtml(r.details)}</div>
      </div>` : ''}
      ${repliedDate ? `
      <div style="border:1px solid rgba(39,201,63,0.2);border-radius:2px;padding:10px;margin-bottom:14px;background:rgba(39,201,63,0.04);">
        <div style="font-family:'Share Tech Mono',monospace;font-size:9px;letter-spacing:2px;color:#27c93f;margin-bottom:6px;">ADMIN REPLY · ${repliedDate}</div>
        <div style="font-family:'Share Tech Mono',monospace;font-size:12px;color:var(--blue-dim);white-space:pre-wrap;word-break:break-word;">${escHtml(r.adminReply)}</div>
      </div>` : ''}
      <div style="margin-bottom:10px;">
        <div style="font-family:'Share Tech Mono',monospace;font-size:9px;letter-spacing:2px;color:var(--blue-dark);margin-bottom:6px;">STATUS</div>
        <select id="req-detail-status" style="background:var(--bg-terminal);border:1px solid var(--blue-dark);color:var(--blue-dim);font-family:'Share Tech Mono',monospace;font-size:11px;padding:5px 8px;border-radius:2px;width:100%;cursor:pointer;">
          ${statusOptions.map(s => `<option value="${s}" ${r.status===s?'selected':''}>${s.toUpperCase().replace('_',' ')}</option>`).join('')}
        </select>
      </div>
      <div style="margin-bottom:10px;">
        <div style="font-family:'Share Tech Mono',monospace;font-size:9px;letter-spacing:2px;color:var(--blue-dark);margin-bottom:6px;">REPLY TO CUSTOMER</div>
        <textarea id="req-detail-reply" style="width:100%;min-height:90px;background:rgba(0,0,0,0.3);border:1px solid var(--blue-dark);color:var(--blue);font-family:'Share Tech Mono',monospace;font-size:12px;padding:8px;border-radius:2px;resize:vertical;outline:none;box-sizing:border-box;" placeholder="Type your reply...">${escHtml(r.adminReply||'')}</textarea>
      </div>
      <div id="req-detail-msg" style="font-family:'Share Tech Mono',monospace;font-size:11px;min-height:16px;margin-bottom:8px;"></div>
      <button id="req-detail-save" style="background:transparent;border:1px solid var(--blue-dark);color:var(--blue-dim);font-family:'Share Tech Mono',monospace;font-size:11px;letter-spacing:2px;padding:8px 20px;border-radius:2px;cursor:pointer;transition:all 0.2s;">SAVE &amp; REPLY</button>
    `;

    document.getElementById('req-back-btn').addEventListener('click', () => { openRequestId = null; renderRequests(); });

    document.getElementById('req-detail-save').addEventListener('click', async () => {
      const status = document.getElementById('req-detail-status').value;
      const adminReply = document.getElementById('req-detail-reply').value.trim();
      const msgEl = document.getElementById('req-detail-msg');
      const btn = document.getElementById('req-detail-save');
      btn.disabled = true; btn.textContent = 'SAVING...';
      try {
        const res = await adminFetch(`/api/admin/service-requests/${r._id}`, {
          method: 'PUT', body: JSON.stringify({ status, adminReply }),
        });
        const data = await res.json();
        if (res.ok) {
          const idx = allRequests.findIndex(x => x._id === r._id);
          if (idx !== -1) allRequests[idx] = data.request;
          openRequestId = data.request._id;
          msgEl.style.color = '#27c93f'; msgEl.textContent = 'SAVED.';
          setTimeout(() => renderRequests(), 800);
        } else {
          msgEl.style.color = '#FF4500'; msgEl.textContent = data.error || 'Save failed.';
        }
      } catch { msgEl.style.color = '#FF4500'; msgEl.textContent = 'Connection error.'; }
      btn.disabled = false; btn.textContent = 'SAVE & REPLY';
    });
  }

  // Filter buttons
  document.querySelectorAll('.req-filter-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.req-filter-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      activeFilter = btn.dataset.filter;
      renderRequests();
    });
  });

  function escHtml(s) {
    return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
  }
});
