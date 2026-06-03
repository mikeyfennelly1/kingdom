# AI Interaction Log - Fionn

This file records significant AI-assisted development sessions attributed to Fionn's work on the Kingdom project. It follows the structure of `docs/ai-log.md` and is reconstructed from the commit history, changed files, and project context rather than copied from a verbatim chat transcript.

The aim of this log is academic transparency: it explains where AI was used as an implementation and review aid, what technical direction was set by the developer, what output was accepted or modified, and where limitations or risks remained.

---
## Session - 2026-05-21 19:00

### What was asked
The developer asked the AI to implement a defined CLI/authentication improvement plan: add an interactive `kdctl` shell that preserved authenticated state, replace insecure password handling with libsodium password hashing, and remove command paths that bypassed the intended security flow.

### What AI produced
- An interactive `kdctl` shell that could preserve login state across commands.
- Login/session handling in the CLI so users did not need to repeatedly pass credentials.
- Password hashing with libsodium instead of storing or comparing raw passwords.
- Cleanup of insecure `kdctl` subcommands that exposed unsafe flows.
- README updates reflecting the safer CLI usage.

### Key prompts and responses

**Prompt:** Implement an interactive `kdctl` shell around the existing CLI11 structure, preserving a logged-in session token across commands without changing the server API unnecessarily.
**Response summary:** The AI suggested structuring the CLI around a session object that stores the bearer token after login and reuses it for later commands. This was implemented in the `kdctl` entry point and aligned with the existing CLI11 command structure.

**Prompt:** Replace plaintext/ad hoc password handling with libsodium password hashing, keeping hashing and verification inside the server authentication path.
**Response summary:** The AI recommended libsodium password hashing rather than ad hoc hashing. The implementation introduced secure password hashing on signup and verification on login, keeping password handling inside the server path.

**Prompt:** Audit the `kdctl` command surface and remove or reroute commands that let users perform protected actions outside the authenticated client flow.
**Response summary:** The AI identified that some direct commands undermined the login/session model and suggested removing them or forcing them through authenticated client calls.

### Design decisions made

- **Interactive session over repeated credentials:** Keeping an in-memory session token made demos and manual testing easier while avoiding repeated password prompts.
- **libsodium for password hashing:** This matched the project's existing cryptographic dependency and avoided building a custom password scheme.
- **Remove unsafe tools instead of hiding them:** Deleting insecure command paths reduced accidental misuse and made the demonstrated security story clearer.

### Critical evaluation

- **Accepted as-is:** The interactive shell direction was a good fit for the CLI-heavy project and improved usability.
- **Accepted as-is:** Using libsodium for password hashing was the correct security choice.
- **Modified:** Some CLI command cleanup required human review to avoid removing useful commands that were still needed for testing.

### Limitations / what AI got wrong or missed

- The early session model improved usability but did not yet provide complete end-to-end authorization on every server route.
- Password hashing addressed credential storage but did not by itself solve transport security, token lifetime, or access-control gaps.

---
## Session - 2026-05-23 16:00

### What was asked
The developer asked the AI to implement the cryptographic identity foundation for end-to-end encrypted messaging from a defined design: client-held user keypairs, encrypted private-key storage, login-time key recovery, JWT authentication, and public-key lookup routes.

### What AI produced
- X25519-style public/private keypair generation during signup.
- Client-side encrypted private-key storage using the user's password-derived key.
- Login-time private-key decryption in `kdctl`.
- JWT authentication following the approach from lectures.
- Server routes and database methods allowing clients to fetch another user's public key.
- HKDF derivation for local key encryption.
- Trust-on-first-use handling for users' public keys on the client.

### Key prompts and responses

**Prompt:** Implement signup-time client identity key generation, send only the public key to the server, and store the private key locally in encrypted form.
**Response summary:** The AI proposed creating the keypair client-side, sending only the public key to the server, and storing the encrypted private key locally. This kept private material out of the server database and matched the end-to-end encryption requirement.

**Prompt:** Implement login-time private-key recovery so the client unlocks local identity material only after successful authentication.
**Response summary:** The AI added login-time key recovery so that after a successful login the client could unlock the local private key using the supplied password and keep it available for messaging operations.

**Prompt:** Implement an authenticated public-key lookup route, database method, and client wrapper so message encryption can obtain a recipient key through the normal API.
**Response summary:** The AI added a server endpoint, database lookup, and client method for public-key retrieval. This allowed later messaging code to encrypt to a recipient without requiring out-of-band key exchange.

**Prompt:** Implement lecture-style signed JWT bearer authentication and integrate it with protected client/server requests.
**Response summary:** The AI suggested bearer-token authentication with signed JWTs and a server-side secret. This was added to the authentication flow and reused by protected client requests.

### Design decisions made

- **Private key never sent to server:** The server stores public keys, while the private key remains local and encrypted at rest.
- **Password-derived local key:** The user's password is used to derive a key for decrypting local private material after authentication.
- **TOFU for public keys:** Trust-on-first-use was chosen as a practical project-level compromise. It gives basic key continuity without requiring a full certificate authority or Web-of-Trust design.
- **JWT bearer tokens:** JWTs gave a simple stateless auth mechanism that worked cleanly with the C++ HTTP client/server design.

### Critical evaluation

- **Accepted as-is:** The client-side keypair and encrypted private-key design aligned well with the privacy goals of the project.
- **Accepted as-is:** Public-key lookup was necessary for later encrypted message sending and was implemented across all required layers.
- **Modified:** TOFU needed careful wording in the crypto report because it does not prove first-contact identity; it only detects later key changes.

### Limitations / what AI got wrong or missed

- The first version of the key infrastructure did not yet include a full X3DH-style session setup or message ratchet.
- TOFU is vulnerable on first contact if the server or network is already malicious.
- JWT authentication still needed consistent route-level enforcement and token cleanup in later work.

---
## Session - 2026-05-25 14:00

### What was asked
The developer asked the AI to complete specific missing pieces of the end-to-end encryption protocol work: metadata-bound message authentication, X3DH-style setup, local message caching, and tests around security predicates.

### What AI produced
- X3DH message setup code and session establishment support.
- Message authentication binding to conversation ID, sender ID, and recipient ID using MACs.
- HKDF and local key derivation improvements.
- A local `MessageStore` implementation for cached messages.
- Client support for reading sent messages locally.
- Expanded security tests around the authentication and cryptographic flows.

### Key prompts and responses

**Prompt:** Implement the X3DH-style session setup and local message cache using the existing `LocalKeyStore` and client/database layering, keeping cryptographic operations inside `libkd`.
**Response summary:** The AI proposed separating long-term identity material from per-conversation session material and storing enough local state to decrypt and display messages after send/receive. The implementation touched `LocalKeyStore`, `MessageStore`, client calls, and controller/database support.

**Prompt:** Bind encrypted messages to conversation, sender, and recipient metadata in the authenticated input so ciphertext cannot be replayed into a different context unnoticed.
**Response summary:** The AI recommended including the conversation ID and participant IDs in the authenticated data/MAC input so ciphertext could not be replayed or moved into a different context unnoticed.

**Prompt:** Extend the security tests around JWT validation and failure cases so the protocol changes are covered by repeatable checks rather than manual testing only.
**Response summary:** The AI added and adjusted security tests to cover JWT validation and failure cases, making the cryptographic changes less dependent on manual testing.

### Design decisions made

- **Authenticated metadata binding:** Conversation ID, sender ID, and recipient ID are included in integrity checks to prevent message-context confusion.
- **Local message cache:** The client keeps local message state so users can read sent messages without requiring the server to know plaintext.
- **Library-owned crypto:** Cryptographic operations stayed in `libkd` rather than being scattered through GUI or CLI code.

### Critical evaluation

- **Accepted as-is:** Binding ciphertext to message metadata was a strong security improvement and directly helped the cryptography marks.
- **Accepted as-is:** Moving message persistence into a dedicated `MessageStore` made later local encryption easier.
- **Modified:** Some tests required adjustment after shared JWT helpers changed and after the message flow evolved.

### Limitations / what AI got wrong or missed

- X3DH-style setup adds complexity and needs careful documentation so graders can see what security properties are actually achieved.
- Local plaintext caching creates risk unless the local store is encrypted, which was addressed in a later session.
- Some generated changes were broad and needed human review to ensure they matched the existing architecture.

---
## Session - 2026-05-28 15:00

### What was asked
The developer asked the AI to implement a set of targeted lifecycle and operational-security fixes: resolve CI path-casing failures, make logout satisfy server validation, add message deletion and access revocation, remove password logging, and clean up unused code.

### What AI produced
- CI fixes for case-sensitive build paths.
- Logout request fix using an empty JSON body that passed the server's validation checks.
- Message delete functionality across CLI, client library, controller, database, and tests.
- Message access revocation functionality across the same layers.
- A fix preventing user passwords from being logged server-side.
- Removal of unused code.

### Key prompts and responses

**Prompt:** Diagnose the CI-only build failures and correct path or include casing so the project builds on a case-sensitive runner.
**Response summary:** The AI identified file/path casing mismatches that passed locally but failed in CI on a case-sensitive environment, then updated references to match the repository paths.

**Prompt:** Implement explicit delete and revoke operations across the database, controller, client, CLI, and tests, preserving ownership checks.
**Response summary:** The AI added database methods, controller routes, client wrappers, CLI commands, and tests. The resulting work gave users meaningful control over stored message records and recipient access.

**Prompt:** Audit authentication logging and remove any sensitive credential material while keeping useful non-secret debug output.
**Response summary:** The AI found a log statement that included sensitive credential material and replaced it with safer logging that preserved debugging value without exposing secrets.

### Design decisions made

- **Delete and revoke as explicit actions:** Message lifecycle controls were implemented as first-class API/client operations rather than hidden database updates.
- **Sensitive-data logging ban:** Passwords and private material should never appear in application logs, even during development.
- **CI fixes kept small:** The casing changes were deliberately narrow to avoid unrelated build churn.

### Critical evaluation

- **Accepted as-is:** The password logging fix was necessary and low risk.
- **Accepted as-is:** Delete/revoke support improved both security and user-facing completeness.
- **Modified:** Access revocation needed to be checked against the actual authorization model so users could not revoke or delete records they did not own.

### Limitations / what AI got wrong or missed

- Deleting or revoking access at the application layer does not erase data already cached by a recipient.
- The logout validation fix solved the immediate issue but also showed that request validation rules needed to be more consistent.
- Message lifecycle changes needed regression testing because they touched many layers.

---
## Session - 2026-05-30 13:00

### What was asked
The developer asked the AI to implement final hardening tasks before submission: enforce stronger password rules, encrypt the local message store, fix one-time prekey consumption access control, refactor repeated controller logic, centralize magic strings, and add GUI message forwarding.

### What AI produced
- Secure password enforcement in server validation and GUI login/signup flow.
- E2E tests covering password validation behavior.
- Encrypted local message storage in `MessageStore`.
- One-time prekey consumption access-control fixes.
- A large controller refactor to reduce repeated logic.
- Centralized constants replacing magic strings.
- GUI support for forwarding messages.

### Key prompts and responses

**Prompt:** Implement the agreed password policy server-side and mirror it in the GUI for user feedback, with tests proving weak passwords are rejected.
**Response summary:** The AI added server-side validation and GUI feedback so weak passwords were rejected consistently. Tests were added to prove the expected behavior.

**Prompt:** Change the local `MessageStore` so cached message content is encrypted at rest instead of persisting plaintext.
**Response summary:** The AI changed `MessageStore` so cached message content was protected at rest rather than left as plaintext. This closed a major gap created by the earlier local cache design.

**Prompt:** Review one-time prekey consumption and update the controller/database behavior so keys cannot be consumed by the wrong user or reused incorrectly.
**Response summary:** The AI updated controller/database behavior to ensure one-time prekeys could not be consumed by the wrong user or reused improperly.

**Prompt:** Implement message forwarding in the Qt GUI by reusing the existing encrypted send path and fitting it into the current `MainWindow` state model.
**Response summary:** The AI implemented forwarding in the Qt GUI, updating `MainWindow` state and UI behavior so users could select and forward messages through the existing message send path.

### Design decisions made

- **Server-side password enforcement is authoritative:** GUI validation improves usability, but the server still rejects weak passwords.
- **Local cache must be encrypted:** Since end-to-end encryption keeps plaintext out of the server, any local plaintext cache must have its own protection.
- **Constants over repeated strings:** Centralizing route names, validation keys, and security labels reduced typo risk and improved maintainability.
- **GUI forwarding uses existing send path:** Reusing the send flow avoided inventing a separate forwarding protocol.

### Critical evaluation

- **Accepted as-is:** Encrypting the local message store materially improved the security story.
- **Accepted as-is:** Password rules and tests made the application easier to defend in assessment.
- **Modified:** The controller refactor was broad and needed review to ensure behavior stayed the same while structure improved.
- **Modified:** Forwarding required UI iteration because GUI state and conversation selection are easy places to introduce edge-case bugs.

### Limitations / what AI got wrong or missed

- Local encryption depends on key management; losing the password or local key material may make cached messages unrecoverable.
- Forwarded messages need clear security semantics because forwarding can intentionally disclose content to another user.
- Large refactors close to submission increase regression risk unless backed by tests.

---
## Session - 2026-05-31 18:00

### What was asked
The developer asked the AI to refine the GUI interaction model so it behaved more like a live messaging client: poll for new conversations, fix conversation naming, and improve message/conversation refresh behavior.

### What AI produced
- Polling logic in the Qt login/main window flow.
- Automatic refresh for new conversations.
- Conversation naming fixes so displayed names were clearer and more consistent.
- GUI state updates to reduce the need for manual refreshes.

### Key prompts and responses

**Prompt:** Implement a lightweight polling refresh for conversations in the Qt UI, avoiding a larger WebSocket/server-sent-events change this late in the project.
**Response summary:** The AI added timers/state hooks so the GUI could periodically fetch conversation data and update the visible conversation list.

**Prompt:** Correct conversation display-name logic in `MainWindow` so the UI shows stable, readable labels instead of raw or inconsistent identifiers.
**Response summary:** The AI adjusted how conversation labels were generated and stored in `MainWindow`, improving readability in the UI.

### Design decisions made

- **Polling over push:** Polling was chosen because it was simpler than adding WebSockets or server-sent events late in the project.
- **GUI owns display naming:** The frontend resolves user-facing names while the backend remains focused on IDs and message data.

### Critical evaluation

- **Accepted as-is:** Polling improved the demo experience substantially with low server impact.
- **Modified:** Polling intervals and refresh behavior needed tuning to avoid stale UI without excessive requests.

### Limitations / what AI got wrong or missed

- Polling is less efficient than a push-based design and may miss instant updates between intervals.
- GUI refresh code can race with user selection if not carefully managed.

---
## Session - 2026-06-01 20:00

### What was asked
The developer asked the AI to rewrite the cryptography report against the actual implementation, with instructions to describe the protocol accurately, avoid overstating guarantees, and make limitations explicit.

### What AI produced
- A rewritten `docs/reports/crypto.md`.
- Clearer descriptions of password hashing, key generation, local key encryption, TOFU, X3DH-style setup, MAC binding, local encrypted message storage, and known limitations.
- More assessment-focused explanation linking implementation choices to cryptography marking criteria.

### Key prompts and responses

**Prompt:** Rewrite `docs/reports/crypto.md` from the implemented code paths, clearly separating achieved security properties, limitations, and assessment-focused explanation.
**Response summary:** The AI used the implemented code paths and commit history to restructure the report around actual security mechanisms rather than planned features. It emphasized what the project protects against, what it does not protect against, and where future improvements would be needed.

### Design decisions made

- **Document implemented behavior, not ideal behavior:** The report focuses on what the code actually does.
- **State limitations directly:** TOFU first-contact risk, local key recovery limits, and protocol simplifications are acknowledged rather than hidden.
- **Map features to marks:** The report explains why the implementation demonstrates applied cryptography concepts.

### Critical evaluation

- **Accepted as-is:** Rewriting the report improved clarity and made the cryptography work easier to assess.
- **Modified:** Some wording needed to avoid overstating formal security guarantees.

### Limitations / what AI got wrong or missed

- AI-generated documentation can sound more confident than the implementation deserves, so human review was required to keep claims precise.
- The report still depends on tests and demos proving the code paths work in practice.

---
## Session - 2026-06-02 15:00

### What was asked
The developer asked the AI to update the penetration-test remediation work: harden server validation, improve token cleanup, verify security predicates, and document remediated findings only where code changes supported the claim.

### What AI produced
- Server validation hardening in `Controller.cc`.
- Database support for token cleanup.
- Security predicate improvements.
- Shared constants for validation/security behavior.
- Updated pentest report entries showing which findings were remediated and how.

### Key prompts and responses

**Prompt:** Harden server-boundary validation and authentication-token cleanup, centralizing repeated security constants where it makes the controller easier to audit.
**Response summary:** The AI added stricter validation paths, improved cleanup for expired or invalid authentication tokens, and moved repeated constants into a shared header. This reduced both security risk and duplicated logic.

**Prompt:** Update the pentest report so each remediated finding is tied to concrete code changes, and avoid marking issues fixed unless the implementation supports it.
**Response summary:** The AI rewrote the pentest report to distinguish original findings from remediated issues, connecting each fix to concrete code changes.

### Design decisions made

- **Validation at the server boundary:** The server remains the final authority even when the GUI or CLI also validates input.
- **Centralized constants:** Security-sensitive string values and limits are easier to audit when defined once.
- **Pentest report tied to code:** Remediation claims reference implemented changes rather than general intentions.

### Critical evaluation

- **Accepted as-is:** Validation hardening and token cleanup were important final security fixes.
- **Accepted as-is:** Updating the pentest report made the security work easier for markers to verify.
- **Modified:** Some report language needed to be careful: a finding should only be marked remediated if the corresponding code change actually exists.

### Limitations / what AI got wrong or missed

- Final hardening happened close to submission, so regression testing remained important.
- Validation rules can become inconsistent across GUI, CLI, and server unless kept centralized.
- Pentest documentation is only credible when paired with test evidence or reproducible steps.

---
## Overall Reflection

Across these sessions, AI assistance was most useful as a guided implementation accelerator across multiple project layers: server controller code, database methods, client wrappers, CLI commands, Qt GUI updates, tests, and documentation. The technical direction came from the project requirements and developer decisions; AI output was used to draft, implement, cross-check, and iterate on those decisions.

Human judgement remained necessary in several places:

- deciding which AI-generated designs were proportionate for the project scope;
- checking that cryptography claims did not overstate the actual protocol;
- reviewing broad controller refactors for regressions;
- validating that documentation matched code rather than planned features;
- testing GUI behavior manually where automated coverage was limited.

The main risk in the AI-assisted work was overconfidence: generated code and documentation often needed narrowing, verification, and clearer limitation statements. The final implementation is stronger because the AI output was treated as a draft to inspect and adapt rather than as unquestioned final code.
