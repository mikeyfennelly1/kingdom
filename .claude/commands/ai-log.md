Read the current conversation history in full. Then append a new entry to `docs/ai-log.md` (create the file if it doesn't exist).

Each entry must follow this exact structure:

```
---
## Session — {DATE} {TIME}

### What was asked
A concise summary of what the user asked Claude to do in this session.

### What Claude produced
A bullet list of the concrete outputs — files created/edited, decisions made, explanations given.

### Key prompts and responses
Pick the 2-4 most significant exchanges. For each write:
**Prompt:** (paraphrase of what the user asked)
**Response summary:** (what Claude said or did, including any code generated)

### Design decisions made
Any architectural or implementation choices that were made during the session, and the reasoning behind them.

### Critical evaluation
Identify anything in Claude's output that was:
- Accepted as-is and why
- Modified or corrected, and what changed
- Rejected and why

### Limitations / what Claude got wrong or missed
Honest assessment of gaps or errors in Claude's output this session.
```

Use today's actual date and time. Do not invent details — base everything strictly on what actually happened in the conversation. Write in plain, direct language suitable for an academic submission. Append to the file, do not overwrite previous entries.
