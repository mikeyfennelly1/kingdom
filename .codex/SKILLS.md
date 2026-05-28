# Codex Skill Commands

Custom slash-command prompts for the Kingdom project. Run these inside a Codex session in the project directory.

---

## /ai-log

Summarises the current Codex session and appends a structured entry to `docs/ai-log.md`.

Run this at the end of every Codex session. It builds up the AI artefacts log required for submission: what you asked, what Codex produced, design decisions made, and critical evaluation of the output.

```
/ai-log
```

---

## Notes

- The command must append to `docs/ai-log.md`, not overwrite previous entries.
- The entry must be based only on what actually happened in the current conversation.
- Use plain, direct language suitable for an academic submission.
