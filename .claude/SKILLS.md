# Claude Skill Commands

Custom slash commands for the Kingdom project. Run these inside a Claude Code session in the project directory.

---

## /ai-log

Summarises the current Claude session and appends a structured entry to `docs/ai-log.md`.

Run this at the end of every Claude session. It builds up the AI artefacts log required for submission — what you asked, what Claude produced, design decisions made, and critical evaluation of the output.

```
/ai-log
```

---

## /critique

Reads the full spec and the entire codebase, then writes an honest gap analysis to `docs/critique.md`. Gives a grade estimate per subject and lists exactly what is implemented, what is stubbed, and what is missing.

```
/critique
```

---

## /report

Generates one of the required submission documents. Pass the report type as an argument.

```
/report cover       → docs/reports/cover.md
/report crypto      → docs/reports/crypto.md
/report pentest     → docs/reports/pentest.md
/report network     → docs/reports/network.md
```

| Report | What it is | Required by |
|---|---|---|
| `cover` | Cover document — group info, contributions, repo URL | Submission requirements |
| `crypto` | Cryptographic design document (2-6 pages) — threat model, construction walkthrough, primitive justifications | Cryptography (O'Brien) |
| `pentest` | Penetration testing and vulnerability report | Networks & Cybersecurity (Burkley) |
| `network` | Network architecture documentation | Networks & Cybersecurity (Burkley) |

Running a report command multiple times will overwrite the previous version — do this as the project progresses to keep documents up to date.

---

## Notes

- All commands read the actual codebase before writing — outputs reflect what is genuinely implemented, not what is planned
- Personal details (names, student IDs, contribution percentages) will be placeholders that you fill in manually
- Run `/report crypto` early — the crypto design doc takes the most work to complete and O'Brien requires parameter-level justifications with RFC citations
