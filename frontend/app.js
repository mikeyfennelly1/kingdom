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

// ─── Crypto helpers ───────────────────────────────────────────────────────────
async function sodiumReady() {
  await sodium.ready;
}

function generateKeyPair() {
  return sodium.crypto_box_keypair();
}

function deriveKEK(password, salt, opsLimit, memLimit) {
  return sodium.crypto_pwhash(
    sodium.crypto_aead_xchacha20poly1305_ietf_KEYBYTES,
    password,
    salt,
    opsLimit,
    memLimit,
    sodium.crypto_pwhash_ALG_ARGON2ID13,
  );
}

function buildKeyFile(username, publicKey, privateKey, password) {
  const salt = sodium.randombytes_buf(sodium.crypto_pwhash_SALTBYTES);
  const opsLimit = sodium.crypto_pwhash_OPSLIMIT_INTERACTIVE;
  const memLimit = sodium.crypto_pwhash_MEMLIMIT_INTERACTIVE;

  const kek = deriveKEK(password, salt, opsLimit, memLimit);
  const nonce = sodium.randombytes_buf(sodium.crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
  const ad = new TextEncoder().encode(username);

  const ciphertext = sodium.crypto_aead_xchacha20poly1305_ietf_encrypt(
    privateKey, ad, null, nonce, kek,
  );
  sodium.memzero(kek);

  return {
    version: 1,
    username,
    publicKey: sodium.to_base64(publicKey, sodium.base64_variants.ORIGINAL),
    privateKey: {
      algorithm: 'XChaCha20-Poly1305',
      ciphertext: sodium.to_base64(ciphertext, sodium.base64_variants.ORIGINAL),
      nonce: sodium.to_base64(nonce, sodium.base64_variants.ORIGINAL),
    },
    kdf: {
      algorithm: 'Argon2id',
      salt: sodium.to_base64(salt, sodium.base64_variants.ORIGINAL),
      opsLimit,
      memLimit,
    },
  };
}

function unlockKeyFile(keyFile, password) {
  if (keyFile.version !== 1) throw new Error('Unsupported key file version');

  const salt = sodium.from_base64(keyFile.kdf.salt, sodium.base64_variants.ORIGINAL);
  const nonce = sodium.from_base64(keyFile.privateKey.nonce, sodium.base64_variants.ORIGINAL);
  const ciphertext = sodium.from_base64(keyFile.privateKey.ciphertext, sodium.base64_variants.ORIGINAL);
  const ad = new TextEncoder().encode(keyFile.username);
  const kek = deriveKEK(password, salt, keyFile.kdf.opsLimit, keyFile.kdf.memLimit);

  let privateKeyBytes;
  try {
    privateKeyBytes = sodium.crypto_aead_xchacha20poly1305_ietf_decrypt(
      null, ciphertext, ad, nonce, kek,
    );
  } catch {
    sodium.memzero(kek);
    throw new Error('Wrong password or corrupted key file');
  }
  sodium.memzero(kek);

  return {
    publicKey: sodium.from_base64(keyFile.publicKey, sodium.base64_variants.ORIGINAL),
    privateKey: privateKeyBytes,
  };
}

function encryptMessage(plaintext, senderPrivKey, recipientPubKeyB64) {
  const recipientPk = sodium.from_base64(recipientPubKeyB64, sodium.base64_variants.ORIGINAL);
  const nonce = sodium.randombytes_buf(sodium.crypto_box_NONCEBYTES);
  const msg = new TextEncoder().encode(plaintext);
  const ciphertext = sodium.crypto_box_easy(msg, nonce, recipientPk, senderPrivKey);

  const combined = new Uint8Array(nonce.length + ciphertext.length);
  combined.set(nonce);
  combined.set(ciphertext, nonce.length);
  return sodium.to_base64(combined, sodium.base64_variants.ORIGINAL);
}

function decryptMessage(payloadB64, recipientPrivKey, senderPubKeyB64) {
  try {
    const combined = sodium.from_base64(payloadB64, sodium.base64_variants.ORIGINAL);
    if (combined.length < sodium.crypto_box_NONCEBYTES + sodium.crypto_box_MACBYTES) return null;

    const nonce = combined.slice(0, sodium.crypto_box_NONCEBYTES);
    const ciphertext = combined.slice(sodium.crypto_box_NONCEBYTES);
    const senderPk = sodium.from_base64(senderPubKeyB64, sodium.base64_variants.ORIGINAL);

    const plaintext = sodium.crypto_box_open_easy(ciphertext, nonce, senderPk, recipientPrivKey);
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
  state.publicKeyCache[userId] = data.publicKey;
  return data.publicKey;
}

// ─── Auth ─────────────────────────────────────────────────────────────────────
async function doSignup(username, password) {
  await sodiumReady();
  const kp = generateKeyPair();
  const publicKeyB64 = sodium.to_base64(kp.publicKey, sodium.base64_variants.ORIGINAL);

  const result = await api('POST', '/signup', { username, password, publicKey: publicKeyB64 });

  // Store encrypted key file
  const keyFile = buildKeyFile(username, kp.publicKey, kp.privateKey, password);
  sodium.memzero(kp.privateKey);
  saveKeyFile(keyFile);

  return { result, identity: unlockKeyFile(keyFile, password) };
}

async function doLogin(username, password) {
  await sodiumReady();
  const result = await api('POST', '/login', { username, password });

  // Unlock local key file
  const keyFile = loadKeyFile(username);
  if (!keyFile) {
    throw new Error('No local key file found for this username. Did you sign up on this device?');
  }
  const identity = unlockKeyFile(keyFile, password);
  return { result, identity };
}

function startSession(result, identity, username) {
  state.token = result.token || result.sessionToken;
  state.userId = result.id;
  state.username = username;
  state.identity = identity;
  state.publicKeyCache[result.id] = result.publicKey
    || sodium.to_base64(identity.publicKey, sodium.base64_variants.ORIGINAL);
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

    // Decrypt
    let displayText = null;
    if (state.identity) {
      const senderPkB64 = state.publicKeyCache[msg.senderId];
      if (senderPkB64) {
        displayText = decryptMessage(msg.payload, state.identity.privateKey, senderPkB64);
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
document.addEventListener('DOMContentLoaded', async () => {
  await sodiumReady();

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
  document.getElementById('new-conv-btn').addEventListener('click', () => {
    clearModalError();
    document.getElementById('conv-name-input').value = '';
    document.getElementById('conv-participants-input').value = '';
    document.getElementById('modal-overlay').classList.remove('hidden');
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
    const participantsRaw = document.getElementById('conv-participants-input').value.trim();

    if (!name) {
      showModalError('Conversation name is required');
      return;
    }

    const participantIds = participantsRaw
      ? participantsRaw.split(/\s+/).map(s => parseInt(s, 10)).filter(n => !isNaN(n))
      : [];

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
