import 'dotenv/config';
import express from 'express';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const app = express();
const PORT = process.env.PORT || 3000;

app.use(express.json());
app.use(express.static(join(__dirname, 'public')));

// n8n webhook integration
const N8N_WEBHOOK_URL = process.env.N8N_WEBHOOK_URL;

// Validate that webhook URL is configured
if (!N8N_WEBHOOK_URL) {
  console.error('ERROR: N8N_WEBHOOK_URL environment variable is not set');
  console.error('Please create a .env file with N8N_WEBHOOK_URL or set it in your environment');
  process.exit(1);
}

app.post('/api/chat', async (req, res) => {
  const { message } = req.body;

  if (!message) {
    return res.status(400).json({ error: 'Message is required' });
  }

  try {
    // Forward message to n8n webhook using GET request with query parameter
    const webhookUrl = `${N8N_WEBHOOK_URL}?message=${encodeURIComponent(message)}`;
    const response = await fetch(webhookUrl, {
      method: 'GET',
      signal: AbortSignal.timeout(30000), // 30 second timeout
    });

    if (!response.ok) {
      throw new Error(`n8n webhook returned ${response.status}`);
    }

    // Get response text first to handle empty responses
    const responseText = await response.text();

    let reply;
    if (!responseText || responseText.trim() === '') {
      reply = '[SYSTEM] n8n workflow executed but returned no response. Check your workflow output.';
    } else {
      try {
        const data = JSON.parse(responseText);

        // n8n returns array format: [{ "text": "response" }]
        if (Array.isArray(data) && data.length > 0 && data[0].text) {
          reply = data[0].text;
        } else {
          // Fallback to other common response formats
          reply = data.reply || data.message || data.output || data.text || JSON.stringify(data);
        }
      } catch {
        // If not JSON, use the raw text
        reply = responseText;
      }
    }

    res.json({ reply });
  } catch (error) {
    console.error('Error calling n8n webhook:', error);

    // Provide helpful error messages
    if (error.name === 'AbortError') {
      return res.status(504).json({
        reply: '[ERROR] Request timeout - n8n webhook took too long to respond.'
      });
    }

    if (error.cause?.code === 'ECONNREFUSED') {
      return res.status(503).json({
        reply: '[ERROR] Cannot connect to n8n. Make sure n8n is running on port 5678.'
      });
    }

    res.status(500).json({
      reply: `[ERROR] Failed to process message: ${error.message}`
    });
  }
});

app.listen(PORT, () => {
  console.log(`Server running on http://localhost:${PORT}`);
});
