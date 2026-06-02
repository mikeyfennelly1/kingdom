# Qt GUI Client — How It Works

## Overview

The `kdctl` executable has been converted from a CLI tool into a **Qt6 desktop GUI application**. It replaces the old command-line interface with two windows: a login/signup dialog and a main messaging window. The underlying network logic lives in `libkd` and is unchanged — the GUI is just a new front-end on top of it.

---

## Where It Lives

```
kdctl/
├── src/
│   ├── main.cc          # Entry point — Qt app lifecycle and window sequencing
│   ├── LoginWindow.hh/.cc   # Login/signup dialog
│   └── MainWindow.hh/.cc    # Main chat window (conversations + messages)
kdctl/CMakeLists.txt         # Qt6 dependencies and AUTOMOC/AUTOUIC setup
```

---

## How the Pieces Fit Together

```
main.cc
  └── showLogin()               ← recursive lambda, re-shown on logout
        └── LoginWindow         ← QDialog
              │  on accept
              └── MainWindow    ← QMainWindow
                    │  on loggedOut signal
                    └── showLogin() again
```

`main.cc` manages the window lifecycle with a recursive `showLogin` lambda. When login succeeds, it hands a `Session` struct (userId, username, token, identity key, server URL) to `MainWindow`. When the user logs out, `MainWindow` emits `loggedOut()`, and `showLogin()` is called again to present a fresh `LoginWindow`.

---

## LoginWindow

**File:** `kdctl/src/LoginWindow.cc`

A `QDialog` with three fields: Server URL, Username, Password.

**What it does on submit:**
1. Reads `KD_CA_CERT` env var to find the CA certificate for TLS (same approach as the old CLI).
2. Constructs a `kd::Client` (from `libkd`) pointing at the entered server URL.
3. Calls `client.login()` or `client.signup()` depending on which button was pressed.
4. Calls `kd::LocalKeyStore::loadForLogin()` to load the user's local identity key from `~/.kingdom/keys/`.
5. Packages everything into a `LoginResult` and calls `accept()`, which signals `main.cc` to open `MainWindow`.

Errors from the server (wrong password, network failure, TLS errors) are displayed inline in a styled red label — no message boxes.

---

## MainWindow

**File:** `kdctl/src/MainWindow.cc`

A `QMainWindow` split into a left sidebar (conversations) and a right panel (messages + input).

### Session State

Holds a `Session` struct passed from `LoginWindow`, plus:
- `kd::Client` — HTTP client pointing at the server, auth token pre-loaded
- `kd::MessageStore` — local plaintext cache (avoids re-decrypting on every poll)
- `userCache_` — `map<userId, username>` populated once at startup via `GET /users`
- `activeConversationId_` and `activeRecipientId_` — which conversation is open

### Conversations

- On startup, calls `GET /users/:id/conversations` via `client_->getConversations()`.
- Lists them in `QListWidget` on the left sidebar.
- "New Conversation" button opens a dialog that fetches all users (`GET /users`), lets you pick one, and calls `POST /conversations`.

### Messages

**Loading:**
- Calls `GET /conversations/:id/messages`.
- For each message, determines the decryption `recipientId`:
  - If **incoming** (sender ≠ me): `recipientId = session_.userId` (the server encrypted for me)
  - If **outgoing** (sender = me): `recipientId = activeRecipientId_` (I encrypted for them)
- Calls `decryptOrPlaceholder()`, which first checks `MessageStore` (local cache), then fetches the sender's public key and calls `kd::LocalKeyStore::decryptMessage()`.
- Failed decryptions show `[decryption failed]` rather than crashing.

**Sending:**
1. Fetches recipient's public key via `GET /users/:id/public-key`.
2. Calls `kd::LocalKeyStore::encryptMessage()` with conversation and participant IDs as AAD.
3. Posts the ciphertext to `POST /conversations/:id/messages` with `recipientId` in the body.
4. The server consumes any referenced one-time pre-key transactionally with message creation.
5. Saves the plaintext to `MessageStore` so it renders immediately without re-decrypting.

**Polling:**
- `QTimer` fires every 5 seconds and calls `loadMessages()` on the active conversation.
- `MessageStore` prevents already-decrypted messages from being re-decrypted on each poll.

### Logout

Stops the poll timer, calls `POST /logout` on the server, clears the auth token, emits `loggedOut()`, and closes the window. `main.cc` then re-shows `LoginWindow`.

---

## Build Changes

`kdctl/CMakeLists.txt` now:
- Requires `Qt6::Core` and `Qt6::Widgets`
- Enables `AUTOMOC`, `AUTOUIC`, `AUTORCC` (needed for Qt's signal/slot meta-object system and `.hh` headers)
- Uses `file(GLOB KD_CTL_SOURCES src/*.cc)` so new `.cc` files in `src/` are picked up automatically
- Handles Qt6 location for both Nix (`Qt6_DIR` / `Qt6_PREFIX` env vars) and macOS Homebrew (`/opt/homebrew/opt/qt`)

**To build:** the same `task build` as always. Qt6 must be available — inside `nix develop` it should be on the path. On macOS, install with `brew install qt`.

---

## Environment Variables

| Variable | Purpose |
|---|---|
| `KD_CA_CERT` | Path to CA certificate for TLS. Required when connecting to a server with a self-signed cert (e.g., local dev). |

---

## What Did Not Change

- `libkd` — `kd::Client`, `kd::LocalKeyStore`, `kd::MessageStore`, `kd::Message`, `kd::Conversation` are all unchanged. The GUI calls the exact same API as the old CLI did.
- The server (`kds`) — no changes needed.
- The REST API contract — unchanged.
- E2EE — encryption/decryption logic is entirely inside `libkd`; `MainWindow` just calls it.
