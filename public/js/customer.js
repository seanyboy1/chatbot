// Customer Page Logic
import { initMatrixRain } from './matrix-rain.js';
import { initTerminalChat } from './shared-terminal.js';
import { codingEffect, initNetworkAnimation } from './logo-animation.js';

// Initialize Matrix rain animation
const rain = initMatrixRain('matrix-rain');
if (rain) {
  rain.start();
}

// Initialize terminal chat with customer context
const terminal = initTerminalChat({
  chatWindowId: 'chat-window',
  userInputId: 'user-input',
  sendBtnId: 'send-btn',
  context: 'customer'
});

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

// Skippable sleep — resolves early if skipSignal fires
function skippableSleep(ms, skipSignal) {
  return new Promise(resolve => {
    const t = setTimeout(resolve, ms);
    skipSignal.then(() => { clearTimeout(t); resolve(); });
  });
}

// Splash Screen Animation Sequence
async function playSplashAnimation() {
  const rackIcon = document.querySelector('.rack-icon');
  const splashOptions = document.getElementById('splash-options');
  const fwCablesContainer = document.querySelector('.fw-cables-container');
  const preStatus = document.getElementById('pre-connection-status');
  const overlay = document.getElementById('logo-splash');

  // Skip trigger — clicking anywhere on the overlay skips to the end
  let triggerSkip;
  const skipSignal = new Promise(resolve => { triggerSkip = resolve; });
  overlay.addEventListener('click', triggerSkip, { once: true });

  const wait = ms => skippableSleep(ms, skipSignal);

  // Initialize network animation on rack
  if (rackIcon) initNetworkAnimation(rackIcon);

  // Show "ESTABLISHING CONNECTION..." blinking (4 smooth pulses via CSS animation)
  if (preStatus) {
    preStatus.classList.add('blinking');
    await wait(1.4 * 3 * 1000); // 3 iterations × 1.4s each
    preStatus.classList.remove('blinking');
  }

  // 1. Rack rails appear
  rackIcon.querySelector('.rack-rails-group')?.classList.add('show');
  await wait(400);

  // 1.5. Patch panel slides in and screws drive
  const patchPanelGroup = rackIcon.querySelector('.patch-panel-group');
  patchPanelGroup?.classList.add('show');
  await wait(150);
  patchPanelGroup?.classList.add('screwing');
  await wait(900);

  // 2. Switch unit slides in, then screws drive in
  const switchGroup = rackIcon.querySelector('.switch-group');
  switchGroup?.classList.add('show');
  await wait(150);
  switchGroup?.classList.add('screwing');
  await wait(1000);

  // 3. Firewall unit slides in, then screws drive in
  const firewallGroup = rackIcon.querySelector('.firewall-group');
  firewallGroup?.classList.add('show');
  await wait(150);
  firewallGroup?.classList.add('screwing');
  await wait(1000);

  // 4. Modem unit slides in, then screws drive in
  const modemGroup = rackIcon.querySelector('.modem-group');
  modemGroup?.classList.add('show');
  await wait(150);
  modemGroup?.classList.add('screwing');
  await wait(700);

  // 5. Boxes appear now that rack is fully built
  if (splashOptions) splashOptions.classList.add('show');
  await wait(200);
  document.getElementById('option-chat')?.classList.add('show');
  await wait(200);
  document.getElementById('option-contact')?.classList.add('show');
  await wait(300);

  // 6. Light blue cables draw (patch panel → switch)
  rackIcon.classList.add('patch-panel-wired');
  await wait(350);

  // 6.5. Red patch cables draw (switch → firewall)
  rackIcon.classList.add('cables-active');
  await wait(700);

  // 7. Green cables draw from FW WAN into modem ETH IN
  rackIcon.classList.add('modem-wired');
  await wait(400);

  // 8. Green cables run down from modem to boxes
  if (fwCablesContainer) fwCablesContainer.classList.add('active');
  rackIcon.classList.add('fw-cables-active');

  // Left cable finishes at 0.5s — connect chat box
  await wait(500);
  document.getElementById('option-chat')?.classList.add('cable-connected');

  // Right cable finishes at another 0.5s — connect contact box
  await wait(500);
  document.getElementById('option-contact')?.classList.add('cable-connected');

  // Both cables connected — activate rack LEDs and start all data flow
  rackIcon.classList.add('leds-active');
  if (fwCablesContainer) fwCablesContainer.classList.add('data-active');

  // Reveal the CLIENT PORTAL button now that animation is complete
  document.getElementById('splash-signin')?.classList.add('visible');

  // Wait for user to click an option
  return new Promise((resolve) => {
    // Re-attach click to overlay now to handle option selection
    overlay.removeEventListener('click', triggerSkip);
    // Ensure button is visible even if skip was used
    document.getElementById('splash-signin')?.classList.add('visible');

    document.getElementById('option-chat')?.addEventListener('click', () => resolve('chat'));
    document.getElementById('option-contact')?.addEventListener('click', () => resolve('contact'));
  });
}

async function handleSplashChoice(choice) {
  const splashOverlay = document.getElementById('logo-splash');
  splashOverlay.classList.add('fade-out');
  await sleep(600);

  if (choice === 'chat') {
    window.location.href = '/chat';
  } else if (choice === 'contact') {
    window.location.href = '/contact';
  }
}

function showContactModal() {
  const splashOverlay = document.getElementById('logo-splash');
  const splashContainer = document.querySelector('.splash-logo-container');

  splashContainer.style.transition = 'opacity 0.3s ease, transform 0.3s ease';
  splashContainer.style.opacity = '0';
  splashContainer.style.transform = 'scale(0.9)';

  setTimeout(() => {
    // Reset container so the new content inside is visible
    splashContainer.style.opacity = '1';
    splashContainer.style.transform = 'scale(1)';

    splashContainer.innerHTML = `
      <div class="contact-modal" style="opacity: 0; transform: scale(0.9); transition: all 0.4s ease;">
        <h2 style="font-family: 'Share Tech Mono', monospace; color: var(--blue); font-size: 36px; margin-bottom: 30px; text-shadow: 0 0 15px var(--blue-glow);">CONTACT US</h2>
        <div style="font-family: 'Share Tech Mono', monospace; color: var(--blue-dim); font-size: 16px; line-height: 2; text-align: left; max-width: 400px;">
          <p style="margin: 10px 0;"><span style="color: var(--blue);">Email:</span> support@bluenet.com</p>
          <p style="margin: 10px 0;"><span style="color: var(--blue);">Phone:</span> +1 (555) 123-4567</p>
          <p style="margin: 10px 0;"><span style="color: var(--blue);">Hours:</span> 24/7 Support</p>
        </div>
        <div style="display: flex; gap: 20px; margin-top: 40px;">
          <button id="back-to-options" class="splash-option-box" style="padding: 16px 32px; min-width: 150px;">
            <div class="option-label" style="font-size: 14px;">BACK</div>
          </button>
          <button id="start-chat-now" class="splash-option-box" style="padding: 16px 32px; min-width: 150px;">
            <div class="option-label" style="font-size: 14px;">START CHAT</div>
          </button>
        </div>
      </div>
    `;

    setTimeout(() => {
      const modal = splashContainer.querySelector('.contact-modal');
      if (modal) {
        modal.style.opacity = '1';
        modal.style.transform = 'scale(1)';
      }

      const backBtn = document.getElementById('back-to-options');
      const chatBtn = document.getElementById('start-chat-now');

      if (backBtn) {
        backBtn.addEventListener('click', () => {
          location.reload();
        });
      }

      if (chatBtn) {
        chatBtn.addEventListener('click', async () => {
          splashOverlay.classList.add('fade-out');
          await sleep(800);
          splashOverlay.remove();
          startBootSequence();
        });
      }
    }, 50);
  }, 400);
}

function startBootSequence() {
  const bootLines = document.querySelectorAll('.boot-sequence p');
  bootLines.forEach((line, i) => {
    line.style.opacity = '0';
    setTimeout(() => {
      line.style.opacity = '1';
    }, i * 400);
  });

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
}

// Start the splash animation when page loads
document.addEventListener('DOMContentLoaded', async () => {
  const choice = await playSplashAnimation();
  await handleSplashChoice(choice);
});
