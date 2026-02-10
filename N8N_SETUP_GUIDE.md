# n8n Chatbot Configuration Guide

This guide explains how to configure your n8n workflow to make the chatbot aware of your network engineering business.

## Overview

Your current n8n workflow receives messages from the web app and returns responses. To make it business-aware, you need to add an AI node (like OpenAI or Claude) with a proper system prompt.

## Recommended n8n Workflow Structure

```
Webhook Trigger
    ↓
[Extract message from query parameter]
    ↓
OpenAI/Claude Chat Node (with system prompt)
    ↓
Format Response
    ↓
Webhook Response
```

## Step-by-Step Configuration

### 1. Set Up Your AI Node

In your n8n workflow:

1. **Add an AI Chat node** after your webhook trigger
   - OpenAI (GPT-4, GPT-3.5-turbo)
   - Anthropic Claude
   - Or other LLM provider

2. **Configure the AI node:**
   - Model: `gpt-4` or `gpt-3.5-turbo` (for OpenAI)
   - Model: `claude-3-sonnet` or `claude-3-opus` (for Anthropic)

### 2. Add the System Prompt

In the AI node, find the **System Message** or **System Prompt** field and paste the content from `n8n-system-prompt.md`.

**Quick version (copy this into n8n):**

```
You are a professional lead qualification assistant for a network engineering services company. Your role is to engage with potential clients, understand their networking needs, and qualify them as leads.

We specialize in network engineering with expertise in:
- Cisco Networking (routers, switches, enterprise solutions)
- Network Security (firewalls, VPNs, security assessments)
- Wireless/WiFi (enterprise wireless design, access point deployment)
- Cloud Networking (AWS, Azure, GCP, hybrid cloud, SD-WAN)

Your responsibilities:
1. Greet prospects warmly and professionally
2. Understand their networking needs and pain points
3. Provide relevant information about our services
4. Gather lead qualification information (business size, current infrastructure, timeline, budget)
5. Set expectations for follow-up within 24 hours

Key services we offer:
- Network design and architecture
- Network implementation and deployment
- Security implementations (firewalls, VPNs, IDS/IPS)
- Wireless network deployments
- Cloud networking across AWS/Azure/GCP
- Network optimization and troubleshooting
- Managed network services

Communication style: Professional yet approachable, technical when needed, solution-focused, consultative.

Try to naturally gather: What problem brought them here? Current infrastructure? Business size? Timeline? Any critical issues?

For emergencies: Prioritize getting contact info immediately.

When closing: Thank them, summarize their needs, and mention someone will follow up within 24 hours.
```

### 3. Configure Message Input

Make sure the AI node receives the user's message from the webhook:

- **User Message**: `{{ $json.query.message }}` or however your webhook passes the message parameter

### 4. Enable Conversation Memory (Optional but Recommended)

To maintain context across multiple messages:

1. Add a **Chat Memory** node (Window Buffer Memory or similar)
2. Connect it to your AI node
3. This allows the bot to remember previous messages in the conversation

**Simple memory setup:**
- Memory Type: Window Buffer Memory
- Session ID: Use a unique identifier per user (could be IP address or session token)
- Context Window: 10 messages (stores last 10 message pairs)

### 5. Format the Response

Your workflow should return the response in the format your web app expects:

```json
[
  {
    "text": "{{ $json.output }}"
  }
]
```

Make sure the response structure matches what your app.js expects:
```javascript
if (Array.isArray(data) && data.length > 0 && data[0].text) {
  reply = data[0].text;
}
```

## Example n8n Workflow JSON Structure

```json
{
  "nodes": [
    {
      "parameters": {
        "httpMethod": "GET",
        "path": "webhook-path",
        "responseMode": "responseNode"
      },
      "type": "n8n-nodes-base.webhook",
      "name": "Webhook"
    },
    {
      "parameters": {
        "model": "gpt-4",
        "messages": {
          "values": [
            {
              "role": "system",
              "content": "=YOUR SYSTEM PROMPT HERE"
            },
            {
              "role": "user",
              "content": "={{ $json.query.message }}"
            }
          ]
        }
      },
      "type": "@n8n/n8n-nodes-langchain.openAi",
      "name": "OpenAI"
    },
    {
      "parameters": {
        "respondWith": "json",
        "responseBody": "=[{\"text\": \"{{ $json.output }}\"}]"
      },
      "type": "n8n-nodes-base.respondToWebhook",
      "name": "Response"
    }
  ]
}
```

## Testing Your Configuration

1. **In n8n**: Use the "Test workflow" button with a sample message
2. **From your web app**: Send a test message like "What services do you offer?"
3. **Expected response**: Should mention network engineering, Cisco, security, wireless, cloud

### Test Questions to Verify:

- "What does your company do?" → Should mention network engineering services
- "Do you work with Cisco?" → Should confirm Cisco expertise
- "Can you help with cloud networking?" → Should mention AWS/Azure/GCP capabilities
- "I need help with WiFi" → Should engage about wireless solutions

## Troubleshooting

**Issue**: Bot gives generic responses, doesn't mention network engineering
**Fix**: Check that the system prompt is properly saved in the AI node

**Issue**: Bot doesn't maintain conversation context
**Fix**: Add or verify the Chat Memory node configuration

**Issue**: Response format error in web app
**Fix**: Ensure response format matches `[{"text": "response here"}]`

**Issue**: Slow responses
**Fix**: Consider using gpt-3.5-turbo instead of gpt-4 for faster responses

## Customization

You can further customize by:

1. **Adding your company name**: Replace `[Company Name]` in the system prompt
2. **Specific pricing guidelines**: Add approximate project ranges if appropriate
3. **Service area restrictions**: Mention geographic limitations if applicable
4. **Response time SLAs**: Update the "24 hours" follow-up time
5. **Emergency contact info**: Add actual emergency contact details

## Advanced: Lead Capture

To capture lead information:

1. Add a **Database node** or **Airtable/Google Sheets node**
2. Parse important details from the conversation
3. Store: Name, contact info, company, needs, timestamp
4. Trigger notifications to your sales/engineering team

## Reference Files

- `business-context.md`: Detailed business information
- `n8n-system-prompt.md`: Full system prompt with examples
- `CLAUDE.md`: Overall project documentation

---

**Need Help?**
Test thoroughly with various questions to ensure the bot responds appropriately to your business context!
