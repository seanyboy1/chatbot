// BLUE-NET Logo Animation
// Matrix-style text reveal with data running through

export function typewriterEffect(element, text, speed = 150) {
  return new Promise((resolve) => {
    let index = 0;
    element.textContent = '';

    const interval = setInterval(() => {
      if (index < text.length) {
        element.textContent += text[index];
        index++;
      } else {
        clearInterval(interval);
        resolve();
      }
    }, speed);
  });
}

export function codingEffect(element, finalText, duration = 2000) {
  return new Promise((resolve) => {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*<>/\\[]{}|';
    const letters = finalText.split('');
    let iterations = 0;
    const charsPerLetter = 5; // Number of random chars before settling
    const totalIterations = letters.length * charsPerLetter;
    const intervalTime = duration / totalIterations;

    element.textContent = '';

    const interval = setInterval(() => {
      const currentLetterIndex = Math.floor(iterations / charsPerLetter);

      // Build the string
      element.textContent = letters
        .map((letter, index) => {
          if (index < currentLetterIndex) {
            // Letter is already revealed
            return letter;
          } else if (index === currentLetterIndex) {
            // Currently revealing this letter with random chars
            return chars[Math.floor(Math.random() * chars.length)];
          } else {
            // Not yet reached
            return '';
          }
        })
        .join('');

      iterations++;

      if (iterations >= totalIterations) {
        clearInterval(interval);
        element.textContent = finalText;
        resolve();
      }
    }, intervalTime);
  });
}

export function animateLogoReveal(logoTextElement, duration = 2000) {
  if (!logoTextElement) return;

  const finalText = 'BLUE-NET';
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*アイウエオカキクケコ';
  const textLength = finalText.length;

  // Store original text
  const letters = finalText.split('');
  let iterations = 0;
  const maxIterations = 20; // Number of random character cycles per letter

  // Set initial random characters
  logoTextElement.textContent = letters.map(() =>
    chars[Math.floor(Math.random() * chars.length)]
  ).join('');

  // Add reveal class for pop-out effect
  logoTextElement.classList.add('logo-revealing');

  const interval = setInterval(() => {
    logoTextElement.textContent = letters.map((letter, index) => {
      // Calculate when this letter should be revealed
      const revealPoint = (index / textLength) * maxIterations;

      if (iterations > revealPoint) {
        // Letter is revealed
        return letter;
      }

      // Still cycling random characters
      return chars[Math.floor(Math.random() * chars.length)];
    }).join('');

    iterations++;

    if (iterations > maxIterations) {
      clearInterval(interval);
      logoTextElement.textContent = finalText;

      // Remove revealing class, add revealed class
      setTimeout(() => {
        logoTextElement.classList.remove('logo-revealing');
        logoTextElement.classList.add('logo-revealed');
      }, 300);
    }
  }, duration / maxIterations);
}

export function initNetworkAnimation(svgElement) {
  if (!svgElement) return;

  // Animate the network nodes/lines in the logo
  const lines = svgElement.querySelectorAll('.network-line');
  const nodes = svgElement.querySelectorAll('.network-node');

  // Stagger animation for lines
  lines.forEach((line, index) => {
    line.style.animationDelay = `${index * 0.1}s`;
  });

  // Pulse animation for nodes
  nodes.forEach((node, index) => {
    node.style.animationDelay = `${index * 0.15}s`;
  });
}
