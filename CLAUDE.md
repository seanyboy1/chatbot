# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Matrix-themed terminal chatbot web app. Node.js + Express backend serving a static frontend.

## Commands

- `npm install` — install dependencies
- `npm start` — start the server (default port 3000)
- `npm run dev` — start with `--watch` for auto-reload during development

## Architecture

- `server.js` — Express server. Serves static files from `public/` and forwards chat requests to n8n webhook.
- `public/` — Static frontend assets
  - `index.html` — Landing page with terminal-style chat UI
  - `css/style.css` — Matrix terminal theme (green-on-black, scanlines, glow effects)
  - `js/app.js` — Chat client logic and Matrix rain canvas animation

## Environment Variables

**Required:**
- `N8N_WEBHOOK_URL` — n8n Cloud webhook URL for chat processing

**Setup:**
1. Copy `.env.example` to `.env`
2. Replace the placeholder with your actual n8n Cloud webhook URL
3. Never commit `.env` to git (already in `.gitignore`)

## n8n Integration

The chatbot uses an n8n webhook for message processing:
- Webhook URL configured via `N8N_WEBHOOK_URL` environment variable
- Server forwards messages to n8n and returns the response to the frontend
- Response structure expected from n8n: `[{ "text": "..." }]` (array format)
- Timeout: 30 seconds per request

## Local Development

1. Clone the repository
2. Run `npm install`
3. Create `.env` file with your n8n webhook URL:
   ```
   N8N_WEBHOOK_URL=https://yourinstance.app.n8n.cloud/webhook/your-webhook-id
   ```
4. Run `npm start`
5. Open http://localhost:3000

## Deployment (Vercel)

**Prerequisites:**
- n8n Cloud instance with a public webhook URL
- GitHub repository: https://github.com/seanyboy1/chatbot

**Steps:**
1. Push code to GitHub (see below)
2. Import project in Vercel dashboard or use Vercel CLI
3. Set environment variable in Vercel:
   - Key: `N8N_WEBHOOK_URL`
   - Value: Your n8n Cloud webhook URL
4. Deploy

**GitHub push:**
```bash
git init
git add .
git commit -m "Initial commit"
git remote add origin https://github.com/seanyboy1/chatbot.git
git branch -M main
git push -u origin main
```

## API

`POST /api/chat` — accepts `{ "message": "..." }`, forwards to n8n webhook, returns `{ "reply": "..." }`.
