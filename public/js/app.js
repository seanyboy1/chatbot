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
  ctx.fillStyle = '#00ff41';
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

  setTimeout(() => userInput.focus(), bootLines.length * 400);
});
