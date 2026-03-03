// Shared Terminal Chat Module
// Exports a function to initialize terminal chat functionality

export function initTerminalChat(config) {
  const {
    chatWindowId,
    userInputId,
    sendBtnId,
    context = 'customer', // 'admin' or 'customer'
    onMessageSent = null,
    onMessageReceived = null
  } = config;

  const chatWindow = document.getElementById(chatWindowId);
  const userInput = document.getElementById(userInputId);
  const sendBtn = document.getElementById(sendBtnId);

  if (!chatWindow || !userInput || !sendBtn) {
    console.error('Terminal chat elements not found');
    return null;
  }

  // Store session ID in localStorage for persistence
  let userSessionId = localStorage.getItem('sessionId') || null;

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

    // Callback for message sent
    if (onMessageSent) {
      onMessageSent(text);
    }

    try {
      const res = await fetch('/api/chat', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          message: text,
          sessionId: userSessionId,
          context // Pass context to backend
        }),
      });

      const data = await res.json();
      typing.remove();
      appendMessage(data.reply, 'bot');

      // Callback for message received
      if (onMessageReceived) {
        onMessageReceived(data.reply);
      }
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

  // Get user IP and session ID
  async function getUserIP() {
    try {
      // Check localStorage first
      if (userSessionId) {
        return userSessionId;
      }

      const res = await fetch(`/api/ip?context=${context}`);
      const data = await res.json();
      if (data.sessionId) {
        userSessionId = data.sessionId;
        // Store in localStorage for persistence
        localStorage.setItem('sessionId', userSessionId);
        return userSessionId;
      }
    } catch (error) {
      console.error('Failed to get session:', error);
    }
    return null;
  }

  // Initialize session
  getUserIP();

  return {
    appendMessage,
    sendMessage,
    getUserIP,
    getSessionId: () => userSessionId
  };
}
