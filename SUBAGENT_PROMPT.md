# Sub-Agent Prompt Template

## Project Context
You are working on a Matrix-themed terminal chatbot web application. The project has:
- **Backend**: Node.js + Express server (`server.js`) serving static files and exposing `POST /api/chat` endpoint
- **Frontend**: Static HTML/CSS/JS with Matrix rain animation and terminal-style chat UI
- **Current State**: The `/api/chat` endpoint is a placeholder that echoes messages. The frontend is fully functional and ready to connect to a real chatbot backend.

## Project Structure
```
chatbot/
├── server.js          # Express server (needs AI integration)
├── package.json       # Dependencies (currently just express)
├── public/
│   ├── index.html     # Terminal UI with chat interface
│   ├── css/style.css  # Matrix theme styling
│   └── js/app.js      # Frontend chat logic (already sends POST /api/chat)
└── CLAUDE.md          # Project documentation
```

## Your Task
[SPECIFY THE TASK HERE - Examples below]

### Example Task 1: Integrate AI Chatbot Backend
Integrate an AI chatbot service (OpenAI, Anthropic Claude, or another provider) into the `/api/chat` endpoint in `server.js`. 

**Requirements:**
- Replace the placeholder echo response with actual AI API calls
- Handle API errors gracefully (network failures, rate limits, etc.)
- Maintain the existing API contract: accepts `{ "message": "..." }`, returns `{ "reply": "..." }`
- Add environment variable support for API keys (use `.env` file, don't commit keys)
- Keep responses in character with the Matrix/terminal theme when appropriate
- Add proper error handling and logging

**Files to modify:**
- `server.js` - Replace the `/api/chat` handler
- `package.json` - Add necessary dependencies (e.g., `dotenv`, AI SDK)
- Create `.env.example` - Template for environment variables

### Example Task 2: Add Chat History Persistence
Implement chat history that persists across page refreshes.

**Requirements:**
- Store chat messages in browser localStorage
- Load previous messages on page load
- Add a "Clear History" command or button
- Maintain the Matrix terminal aesthetic

**Files to modify:**
- `public/js/app.js` - Add localStorage logic
- `public/index.html` - Add clear history button if needed

### Example Task 3: Add Command System
Implement a command system (e.g., `/help`, `/clear`, `/theme`, etc.)

**Requirements:**
- Parse commands starting with `/`
- Implement at least 3 commands
- Display command help
- Keep terminal-style feedback

**Files to modify:**
- `public/js/app.js` - Add command parsing and handlers
- `server.js` - Handle server-side commands if needed

## Development Guidelines
- **Commands**: Use `npm start` to run server, `npm run dev` for watch mode
- **Port**: Default is 3000 (configurable via PORT env var)
- **Style**: Maintain the Matrix terminal aesthetic - green-on-black, monospace fonts, terminal-style UI
- **Code Style**: Use ES modules (project uses `"type": "module"`)

## Testing
- Test your changes by running `npm start` and visiting `http://localhost:3000`
- Ensure the frontend can successfully communicate with your backend changes
- Verify error handling works (try disconnecting network, invalid API keys, etc.)

## Notes
- The frontend already handles the chat UI, typing indicators, and message display
- Focus on backend integration or the specific feature requested
- Keep changes minimal and focused on the task
- Follow existing code patterns and style
