/* Kingdom — Frontend
 * End-to-end encrypted messaging UI.
 * Crypto matches the C++ libkd implementation exactly:
 *   - At-rest keys: Argon2id KDF + XChaCha20-Poly1305 AEAD (username as AD)
 *   - Message encryption: crypto_box_easy (X25519 + XSalsa20-Poly1305)
 *   - Payload format: base64(nonce[24] || ciphertext)
 */

// ─── State ──────────────────────────────────────────────────────────────────
const state = {
  token: null,
  userId: null,
  username: null,
  identity: null,        // { publicKey: Uint8Array, privateKey: Uint8Array }
  conversations: [],
  activeConvId: null,
  messages: [],
  publicKeyCache: {},    // userId -> base64 pubkey string
  pollInterval: null,
};

// ─── API ─────────────────────────────────────────────────────────────────────
function serverUrl() {
  return document.getElementById('server-url').value.replace(/\/$/, '');
}

async function api(method, path, body) {
  const headers = { 'Content-Type': 'application/json' };
  if (state.token) headers['Authorization'] = `Bearer ${state.token}`;

  let res;
  try {
    res = await fetch(`${serverUrl()}${path}`, {
      method,
      headers,
      body: body !== undefined ? JSON.stringify(body) : undefined,
    });
  } catch (err) {
    throw new Error(`Network error: ${err.message}. Check the server URL and that TLS is trusted.`);
  }

  const data = await res.json();
  if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
  return data;
}

is th// ─── Crypto ───────────────────────────────────────────────────────────────────
// tweetnacl: nacl.box = X25519+XSalsa20-Poly1305, identical to C++ crypto_box_easy
// Web Crypto API: PBKDF2-SHA256 + AES-256-GCM for at-rest key storage in localStorage

function b64Encode(bytes) {
  return btoa(String.fromCharCode(...bytes));
}

function b64Decode(str) {
  return Uint8Array.from(atob(str), c => c.charCodeAt(0));
}

function generateKeyPair() {
  const kp = nacl.box.keyPair();
  return { publicKey: kp.publicKey, privateKey: kp.secretKey };
}

async function deriveStorageKey(password, salt) {
  const raw = await crypto.subtle.importKey(
    'raw', new TextEncoder().encode(password), 'PBKDF2', false, ['deriveKey'],
  );
  return crypto.subtle.deriveKey(
    { name: 'PBKDF2', salt, iterations: 600000, hash: 'SHA-256' },
    raw,
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt'],
  );
}

async function buildKeyFile(username, publicKey, privateKey, password) {
  const salt = crypto.getRandomValues(new Uint8Array(16));
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const storageKey = await deriveStorageKey(password, salt);
  const ad = new TextEncoder().encode(username);

  const ciphertext = await crypto.subtle.encrypt(
    { name: 'AES-GCM', iv, additionalData: ad },
    storageKey,
    privateKey,
  );

  return {
    version: 2,
    username,
    publicKey: b64Encode(publicKey),
    privateKey: {
      algorithm: 'AES-256-GCM',
      ciphertext: b64Encode(new Uint8Array(ciphertext)),
      iv: b64Encode(iv),
    },
    kdf: { algorithm: 'PBKDF2-SHA256', salt: b64Encode(salt), iterations: 600000 },
  };
}

async function unlockKeyFile(keyFile, password) {
  if (keyFile.version !== 2) throw new Error('Unsupported key file version');

  const salt = b64Decode(keyFile.kdf.salt);
  const iv = b64Decode(keyFile.privateKey.iv);
  const ciphertext = b64Decode(keyFile.privateKey.ciphertext);
  const ad = new TextEncoder().encode(keyFile.username);
  const storageKey = await deriveStorageKey(password, salt);

  let privateKeyBytes;
  try {
    const decrypted = await crypto.subtle.decrypt(
      { name: 'AES-GCM', iv, additionalData: ad },
      storageKey,
      ciphertext,
    );
    privateKeyBytes = new Uint8Array(decrypted);
  } catch {
    throw new Error('Wrong password or corrupted key file');
  }

  return { publicKey: b64Decode(keyFile.publicKey), privateKey: privateKeyBytes };
}

// nacl.box = X25519 + XSalsa20-Poly1305 — identical construction to C++ crypto_box_easy
// payload format: base64(nonce[24] || ciphertext) — matches C++ libkd exactly
function encryptMessage(plaintext, senderPrivKey, recipientPubKeyB64) {
  const recipientPk = b64Decode(recipientPubKeyB64.trim());
  if (recipientPk.length !== 32) {
    throw new Error(`recipient public key decoded to ${recipientPk.length} bytes (expected 32) — key: "${recipientPubKeyB64.substring(0, 20)}..."`);
  }
  if (senderPrivKey.length !== 32) {
    throw new Error(`sender private key is ${senderPrivKey.length} bytes (expected 32)`);
  }
  const nonce = nacl.randomBytes(nacl.box.nonceLength); // 24 bytes
  const msg = new TextEncoder().encode(plaintext);
  const ciphertext = nacl.box(msg, nonce, recipientPk, senderPrivKey);

  const combined = new Uint8Array(nonce.length + ciphertext.length);
  combined.set(nonce);
  combined.set(ciphertext, nonce.length);
  return b64Encode(combined);
}

function decryptMessage(payloadB64, recipientPrivKey, senderPubKeyB64) {
  try {
    const combined = b64Decode(payloadB64);
    if (combined.length < nacl.box.nonceLength + nacl.box.overheadLength) return null;

    const nonce = combined.slice(0, nacl.box.nonceLength);
    const ciphertext = combined.slice(nacl.box.nonceLength);
    const senderPk = b64Decode(senderPubKeyB64);

    const plaintext = nacl.box.open(ciphertext, nonce, senderPk, recipientPrivKey);
    if (!plaintext) return null;
    return new TextDecoder().decode(plaintext);
  } catch {
    return null;
  }
}

// ─── Key storage ─────────────────────────────────────────────────────────────
function keyStorageKey(username) {
  return `kingdom:key:${username}`;
}

function saveKeyFile(keyFile) {
  localStorage.setItem(keyStorageKey(keyFile.username), JSON.stringify(keyFile));
}

function loadKeyFile(username) {
  const raw = localStorage.getItem(keyStorageKey(username));
  return raw ? JSON.parse(raw) : null;
}

// ─── Public key cache ─────────────────────────────────────────────────────────
async function getPublicKey(userId) {
  if (state.publicKeyCache[userId]) return state.publicKeyCache[userId];
  const data = await api('GET', `/users/${userId}/public-key`);
  const key = data.publicKey;
  if (!key || key.trim().length === 0) {
    throw new Error(`User ${userId} has no public key registered. They must sign up or log in via the app first.`);
  }
  const decoded = b64Decode(key.trim());
  if (decoded.length !== 32) {
    throw new Error(`User ${userId} public key is invalid (decoded to ${decoded.length} bytes, expected 32).`);
  }
  state.publicKeyCache[userId] = key.trim();
  return state.publicKeyCache[userId];
}

// ─── Auth ─────────────────────────────────────────────────────────────────────
async function doSignup(username, password) {
  const kp = generateKeyPair();
  const publicKeyB64 = b64Encode(kp.publicKey);

  const result = await api('POST', '/signup', { username, password, publicKey: publicKeyB64 });

  const keyFile = await buildKeyFile(username, kp.publicKey, kp.privateKey, password);
  saveKeyFile(keyFile);

  const identity = await unlockKeyFile(keyFile, password);
  return { result, identity };
}

async function doLogin(username, password) {
  const result = await api('POST', '/login', { username, password });

  const keyFile = loadKeyFile(username);
  if (!keyFile) {
    throw new Error('No local key file found for this username. Did you sign up on this device?');
  }
  const identity = await unlockKeyFile(keyFile, password);
  return { result, identity };
}

function startSession(result, identity, username) {
  state.token = result.token || result.sessionToken;
  state.userId = result.id;
  state.username = username;
  state.identity = identity;
  state.publicKeyCache[result.id] = result.publicKey
    || b64Encode(identity.publicKey);
}

async function doLogout() {
  try { await api('POST', '/logout'); } catch { /* ignore */ }
  clearSession();
}

function clearSession() {
  if (state.pollInterval) clearInterval(state.pollInterval);
  state.token = null;
  state.userId = null;
  state.username = null;
  state.identity = null;
  state.conversations = [];
  state.activeConvId = null;
  state.messages = [];
  state.publicKeyCache = {};
  state.pollInterval = null;
  showView('auth');
}

// ─── Conversations ────────────────────────────────────────────────────────────
async function loadConversations() {
  const data = await api('GET', `/users/${state.userId}/conversations`);
  state.conversations = Array.isArray(data) ? data : [];
  renderConversations();
}

async function createConversation(name, participantIds) {
  const ids = [...new Set([...participantIds, state.userId])];
  return api('POST', '/conversations', { name, participantIds: ids });
}

// ─── Messages ─────────────────────────────────────────────────────────────────
async function loadMessages(convId) {
  const data = await api('GET', `/conversations/${convId}/messages`);
  state.messages = Array.isArray(data) ? data : [];
  state.messages.sort((a, b) => a.timestamp - b.timestamp);

  // Pre-fetch public keys for all senders so decryption works
  const senderIds = [...new Set(state.messages.map(m => m.senderId))];
  await Promise.allSettled(senderIds.map(id => getPublicKey(id)));

  renderMessages();
}

async function sendMessage(convId, text, recipientId) {
  if (!state.identity) throw new Error('Not logged in');
  const recipientPk = await getPublicKey(recipientId);
  const payload = encryptMessage(text, state.identity.privateKey, recipientPk);
  return api('POST', `/conversations/${convId}/messages`, {
    senderId: state.userId,
    payload,
  });
}

async function populateUserPicker() {
  const picker = document.getElementById('user-picker');
  picker.innerHTML = '<div class="user-picker-empty">Loading users…</div>';
  try {
    const users = await api('GET', '/users');
    const others = users.filter(u => u.id !== state.userId);
    if (others.length === 0) {
      picker.innerHTML = '<div class="user-picker-empty">No other users registered yet.</div>';
      return;
    }
    picker.innerHTML = '';
    for (const u of others) {
      const item = document.createElement('label');
      item.className = 'user-picker-item';
      item.innerHTML = `
        <input type="checkbox" value="${u.id}" />
        <span class="picker-name">${escHtml(u.username)}</span>
        <span class="picker-id">#${u.id}</span>
      `;
      picker.appendChild(item);
    }
  } catch (err) {
    picker.innerHTML = `<div class="user-picker-empty">Failed to load users: ${escHtml(err.message)}</div>`;
  }
}

// ─── Rendering ───────────────────────────────────────────────────────────────
function renderConversations() {
  const list = document.getElementById('conv-list');
  if (state.conversations.length === 0) {
    list.innerHTML = '<div style="padding:12px 10px;color:var(--text-muted);font-size:0.8rem">No conversations yet</div>';
    return;
  }

  list.innerHTML = '';
  for (const conv of state.conversations) {
    const item = document.createElement('div');
    item.className = 'conv-item' + (conv.id === state.activeConvId ? ' active' : '');
    item.dataset.id = conv.id;

    const others = (conv.participantIds || []).filter(id => id !== state.userId);
    const meta = others.length === 0
      ? 'Just you'
      : `${conv.participantIds.length} participants`;

    item.innerHTML = `
      <div class="conv-item-name">${escHtml(conv.name)}</div>
      <div class="conv-item-meta">${meta}</div>
    `;
    item.addEventListener('click', () => selectConversation(conv));
    list.appendChild(item);
  }
}

async function selectConversation(conv) {
  state.activeConvId = conv.id;
  renderConversations(); // update active highlight

  document.getElementById('chat-empty').classList.add('hidden');
  document.getElementById('chat-active').classList.remove('hidden');
  document.getElementById('chat-name').textContent = conv.name;

  const others = (conv.participantIds || []).filter(id => id !== state.userId);
  document.getElementById('chat-participants').textContent =
    `Participants: ${conv.participantIds.join(', ')}`;

  // Recipient selector
  const recipientRow = document.getElementById('recipient-row');
  const recipientSelect = document.getElementById('recipient-select');

  if (others.length === 1) {
    // 2-person conversation — auto-select the other person
    recipientSelect.innerHTML = `<option value="${others[0]}">User ${others[0]}</option>`;
    recipientRow.classList.add('hidden');
  } else if (others.length > 1) {
    recipientSelect.innerHTML = others.map(id =>
      `<option value="${id}">User ${id}</option>`,
    ).join('');
    recipientRow.classList.remove('hidden');
  } else {
    // Solo conversation
    recipientSelect.innerHTML = `<option value="${state.userId}">Yourself</option>`;
    recipientRow.classList.add('hidden');
  }

  // Load messages and start polling
  await loadMessages(conv.id);

  if (state.pollInterval) clearInterval(state.pollInterval);
  state.pollInterval = setInterval(async () => {
    if (state.activeConvId === conv.id) {
      await loadMessages(conv.id).catch(() => {});
    }
  }, 5000);
}

function renderMessages() {
  const container = document.getElementById('messages');
  const scrolledToBottom =
    container.scrollHeight - container.scrollTop - container.clientHeight < 60;

  container.innerHTML = '';

  for (const msg of state.messages) {
    const isOwn = msg.senderId === state.userId;
    const div = document.createElement('div');
    div.className = `message ${isOwn ? 'own' : 'other'}`;

    // Decrypt.
    // nacl.box needs the *other* person's public key regardless of direction:
    //   - You received it → other person = sender → use sender's pubkey
    //   - You sent it     → other person = recipient → use recipient's pubkey
    let displayText = null;
    if (state.identity) {
      let peerPkB64;
      if (isOwn) {
        // Find the other participant in this conversation
        const conv = state.conversations.find(c => c.id === state.activeConvId);
        const others = conv ? conv.participantIds.filter(id => id !== state.userId) : [];
        peerPkB64 = others.length === 1 ? state.publicKeyCache[others[0]] : null;
      } else {
        peerPkB64 = state.publicKeyCache[msg.senderId];
      }
      if (peerPkB64) {
        displayText = decryptMessage(msg.payload, state.identity.privateKey, peerPkB64);
      }
    }

    const sender = isOwn ? 'You' : `User ${msg.senderId}`;
    const time = new Date(msg.timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    const blockchainHtml = msg.blockchainDigest
      ? `<span class="chain-badge" title="Recorded on Sepolia: ${escHtml(msg.blockchainDigest)}">&#9744; on-chain</span>`
      : '';

    div.innerHTML = `
      <div class="message-sender">${escHtml(sender)}</div>
      <div class="message-bubble${displayText === null ? ' failed' : ''}">
        ${displayText !== null ? escHtml(displayText) : '&#128274; [encrypted — cannot decrypt]'}
      </div>
      <div class="message-footer">
        <span>${time}</span>
        ${blockchainHtml}
      </div>
    `;
    container.appendChild(div);
  }

  if (scrolledToBottom || state.messages.length <= 5) {
    container.scrollTop = container.scrollHeight;
  }
}

// ─── Views ────────────────────────────────────────────────────────────────────
function showView(view) {
  document.getElementById('view-auth').classList.toggle('hidden', view !== 'auth');
  document.getElementById('view-app').classList.toggle('hidden', view !== 'app');
}

function showAuthError(msg) {
  const el = document.getElementById('auth-error');
  el.textContent = msg;
  el.classList.remove('hidden');
}

function clearAuthError() {
  document.getElementById('auth-error').classList.add('hidden');
}

function showModalError(msg) {
  const el = document.getElementById('modal-error');
  el.textContent = msg;
  el.classList.remove('hidden');
}

function clearModalError() {
  document.getElementById('modal-error').classList.add('hidden');
}

function showApp() {
  document.getElementById('sidebar-username').textContent = state.username;
  document.getElementById('sidebar-userid').textContent = `#${state.userId}`;
  document.getElementById('chat-empty').classList.remove('hidden');
  document.getElementById('chat-active').classList.add('hidden');
  showView('app');
  loadConversations().catch(err => console.error('Failed to load conversations:', err));
}

// ─── Utility ──────────────────────────────────────────────────────────────────
function escHtml(str) {
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function setLoading(btnId, loading) {
  const btn = document.getElementById(btnId);
  if (btn) btn.disabled = loading;
}

// ─── Event wiring ─────────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  // Tab switching
  document.querySelectorAll('.tab').forEach(tab => {
    tab.addEventListener('click', () => {
      document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
      tab.classList.add('active');
      const target = tab.dataset.tab;
      document.getElementById('login-form').classList.toggle('hidden', target !== 'login');
      document.getElementById('signup-form').classList.toggle('hidden', target !== 'signup');
      clearAuthError();
    });
  });

  // Login
  document.getElementById('login-form').addEventListener('submit', async e => {
    e.preventDefault();
    clearAuthError();
    const username = document.getElementById('login-username').value.trim();
    const password = document.getElementById('login-password').value;
    if (!username || !password) return;

    setLoading('login-btn', true);
    try {
      const { result, identity } = await doLogin(username, password);
      startSession(result, identity, username);
      showApp();
    } catch (err) {
      showAuthError(err.message);
    } finally {
      setLoading('login-btn', false);
    }
  });

  // Signup
  document.getElementById('signup-form').addEventListener('submit', async e => {
    e.preventDefault();
    clearAuthError();
    const username = document.getElementById('signup-username').value.trim();
    const password = document.getElementById('signup-password').value;
    if (!username || !password) return;

    setLoading('signup-btn', true);
    try {
      const { result, identity } = await doSignup(username, password);
      startSession(result, identity, username);
      showApp();
    } catch (err) {
      showAuthError(err.message);
    } finally {
      setLoading('signup-btn', false);
    }
  });

  // Logout
  document.getElementById('logout-btn').addEventListener('click', async () => {
    await doLogout();
  });

  // New conversation button
  document.getElementById('new-conv-btn').addEventListener('click', async () => {
    clearModalError();
    document.getElementById('conv-name-input').value = '';
    document.getElementById('modal-overlay').classList.remove('hidden');
    await populateUserPicker();
  });

  // Modal cancel
  document.getElementById('modal-cancel').addEventListener('click', () => {
    document.getElementById('modal-overlay').classList.add('hidden');
  });

  // Modal overlay click to close
  document.getElementById('modal-overlay').addEventListener('click', e => {
    if (e.target === document.getElementById('modal-overlay')) {
      document.getElementById('modal-overlay').classList.add('hidden');
    }
  });

  // Modal create
  document.getElementById('modal-create').addEventListener('click', async () => {
    clearModalError();
    const name = document.getElementById('conv-name-input').value.trim();
    if (!name) { showModalError('Conversation name is required'); return; }

    const checked = document.querySelectorAll('#user-picker input[type=checkbox]:checked');
    const participantIds = Array.from(checked).map(cb => parseInt(cb.value, 10));

    try {
      await createConversation(name, participantIds);
      document.getElementById('modal-overlay').classList.add('hidden');
      await loadConversations();
    } catch (err) {
      showModalError(err.message);
    }
  });

  // Send message
  document.getElementById('send-btn').addEventListener('click', () => sendCurrentMessage());
  document.getElementById('message-input').addEventListener('keydown', e => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      sendCurrentMessage();
    }
  });
});

async function sendCurrentMessage() {
  const input = document.getElementById('message-input');
  const text = input.value.trim();
  if (!text || !state.activeConvId) return;

  const recipientId = parseInt(document.getElementById('recipient-select').value, 10);
  if (isNaN(recipientId)) return;

  const btn = document.getElementById('send-btn');
  btn.disabled = true;
  input.disabled = true;

  try {
    const msg = await sendMessage(state.activeConvId, text, recipientId);
    input.value = '';
    // Fetch sender pubkey if not cached (for our own messages display)
    if (!state.publicKeyCache[msg.senderId]) {
      await getPublicKey(msg.senderId).catch(() => {});
    }
    await loadMessages(state.activeConvId);
  } catch (err) {
    alert(`Failed to send: ${err.message}`);
  } finally {
    btn.disabled = false;
    input.disabled = false;
    input.focus();
  }
}
