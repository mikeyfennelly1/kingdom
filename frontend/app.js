/* Kingdom — Frontend
 * X3DH key establishment + XChaCha20-Poly1305 matching libkd/src/LocalKeyStore.cc
 * on the x3dh-session-setup branch exactly.
 * Protocol constants must match C++ verbatim.
 */

// ─── Protocol constants — must match LocalKeyStore.cc verbatim ─────────────
const BUNDLE_ALGORITHM   = 'KD-X3DH-BUNDLE-V1';
const PAYLOAD_ALGORITHM  = 'KD-X3DH-XCHACHA20POLY1305-V1';
const X3DH_HKDF_INFO     = 'kd-x3dh-message-key-v1';
const X3DH_HKDF_SALT     = 'kd-x3dh-session-salt-v1';
const SIGNED_PK_SIG_INFO = 'kd-x3dh-signed-prekey-signature-v1';
const ONE_TIME_PREKEY_BATCH = 20;

// ─── State ─────────────────────────────────────────────────────────────────
const state = {
  token: null,
  userId: null,
  username: null,
  identity: null,        // LocalIdentity — see shape below
  conversations: [],
  activeConvId: null,
  messages: [],
  bundleCache: {},       // userId (number) → parsed X3DH bundle object
  plaintextCache: {},    // messageId (number) → plaintext string (own sent msgs)
  pollInterval: null,
};

// LocalIdentity shape:
// {
//   publicKey:        Uint8Array  (X25519 identity public key, 32 bytes)
//   privateKey:       Uint8Array  (X25519 identity private key, 32 bytes)
//   signingPublicKey: Uint8Array  (Ed25519 public key, 32 bytes)
//   signingSecretKey: Uint8Array  (Ed25519 secret key, 64 bytes)
//   signedPreKey:     { id: number, publicKey: Uint8Array, privateKey: Uint8Array }
//   oneTimePreKeys:   [{ id: number, publicKey: Uint8Array, privateKey: Uint8Array }]
// }

// ─── libsodium readiness ───────────────────────────────────────────────────
let _sodiumReady = false;
sodium.ready.then(() => { _sodiumReady = true; });

function getSodium() {
  if (!_sodiumReady) throw new Error('libsodium not initialised yet');
  return sodium;
}

// ─── Encoding helpers ──────────────────────────────────────────────────────
// Standard base64 with padding — matches C++ sodium_base64_VARIANT_ORIGINAL

function b64Encode(bytes) {
  return getSodium().to_base64(bytes, sodium.base64_variants.ORIGINAL);
}

function b64Decode(str) {
  return getSodium().from_base64(str.trim(), sodium.base64_variants.ORIGINAL);
}

// Little-endian uint64 — matches C++ appendUint64Le
function uint64LE(value) {
  const buf = new DataView(new ArrayBuffer(8));
  buf.setUint32(0, value >>> 0, true);
  buf.setUint32(4, Math.floor(value / 0x100000000), true);
  return new Uint8Array(buf.buffer);
}

function concatBytes(...arrays) {
  const total = arrays.reduce((n, a) => n + a.length, 0);
  const out = new Uint8Array(total);
  let off = 0;
  for (const a of arrays) { out.set(a, off); off += a.length; }
  return out;
}

// ─── HKDF-SHA256 (Web Crypto API) ─────────────────────────────────────────
// Matches C++ deriveKey(secret, info, salt) — OpenSSL HKDF with SHA-256.
async function hkdfSha256(ikm, infoStr, saltStr) {
  const key = await crypto.subtle.importKey('raw', ikm, 'HKDF', false, ['deriveBits']);
  const bits = await crypto.subtle.deriveBits(
    {
      name: 'HKDF',
      hash: 'SHA-256',
      salt: new TextEncoder().encode(saltStr),
      info: new TextEncoder().encode(infoStr),
    },
    key,
    256,
  );
  return new Uint8Array(bits);
}

// ─── X3DH primitives ───────────────────────────────────────────────────────

// Raw X25519 DH — matches C++ crypto_scalarmult
function dh(privateKey, publicKey) {
  return getSodium().crypto_scalarmult(privateKey, publicKey);
}

// Concatenate DH outputs and run HKDF — matches C++ deriveX3dhKey
async function deriveX3dhKey(dhOutputs) {
  return hkdfSha256(concatBytes(...dhOutputs), X3DH_HKDF_INFO, X3DH_HKDF_SALT);
}

// ─── Signed prekey signature ───────────────────────────────────────────────
// Matches C++ signedPreKeySignatureInput / signSignedPreKey / verifySignedPreKeySignature
// Input: info_text || uint64LE(id) || raw_public_key_bytes

function signedPreKeyInput(id, publicKeyBytes) {
  return concatBytes(
    new TextEncoder().encode(SIGNED_PK_SIG_INFO),
    uint64LE(id),
    publicKeyBytes,
  );
}

function signSignedPreKey(id, publicKeyBytes, signingSecretKey) {
  return getSodium().crypto_sign_detached(signedPreKeyInput(id, publicKeyBytes), signingSecretKey);
}

function verifySignedPreKey(bundle) {
  const s         = getSodium();
  const signingKey = b64Decode(bundle.signingKey);
  const spkPub    = b64Decode(bundle.signedPreKey.publicKey);
  const signature  = b64Decode(bundle.signedPreKey.signature);
  if (!s.crypto_sign_verify_detached(signature, signedPreKeyInput(bundle.signedPreKey.id, spkPub), signingKey)) {
    throw new Error('Signed prekey signature verification failed');
  }
}

function validateBundle(bundle) {
  if (!bundle || bundle.version !== 1 || bundle.algorithm !== BUNDLE_ALGORITHM ||
      !bundle.identityKey || !bundle.signingKey || !bundle.signedPreKey || !bundle.oneTimePreKeys) {
    throw new Error('Unsupported or incomplete X3DH key bundle');
  }
  verifySignedPreKey(bundle);
}

// ─── Key generation ────────────────────────────────────────────────────────

function makePreKey(id) {
  const kp = getSodium().crypto_box_keypair();
  return { id, publicKey: kp.publicKey, privateKey: kp.privateKey };
}

function generateIdentity() {
  const s         = getSodium();
  const identityKp = s.crypto_box_keypair();
  const signingKp  = s.crypto_sign_keypair();
  const signedPreKey   = makePreKey(1);
  const oneTimePreKeys = Array.from({ length: ONE_TIME_PREKEY_BATCH }, (_, i) => makePreKey(i + 1));

  return {
    publicKey:        identityKp.publicKey,
    privateKey:       identityKp.privateKey,
    signingPublicKey: signingKp.publicKey,
    signingSecretKey: signingKp.privateKey,
    signedPreKey,
    oneTimePreKeys,
  };
}

// Public bundle sent to server — matches C++ publicBundle()
function buildPublicBundle(identity) {
  const sig = signSignedPreKey(
    identity.signedPreKey.id,
    identity.signedPreKey.publicKey,
    identity.signingSecretKey,
  );
  return {
    version:   1,
    algorithm: BUNDLE_ALGORITHM,
    identityKey:  b64Encode(identity.publicKey),
    signingKey:   b64Encode(identity.signingPublicKey),
    signedPreKey: {
      id:        identity.signedPreKey.id,
      publicKey: b64Encode(identity.signedPreKey.publicKey),
      signature: b64Encode(sig),
    },
    oneTimePreKeys: identity.oneTimePreKeys.map(pk => ({
      id:        pk.id,
      publicKey: b64Encode(pk.publicKey),
    })),
  };
}

// ─── At-rest key encryption (PBKDF2-SHA256 + AES-256-GCM) ─────────────────
// Browser localStorage only. C++ uses Argon2id+HKDF+XChaCha20-Poly1305 on
// the filesystem — these are separate per-device stores and formats need not match.

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

async function buildKeyFile(username, identity, password) {
  const salt       = crypto.getRandomValues(new Uint8Array(16));
  const iv         = crypto.getRandomValues(new Uint8Array(12));
  const storageKey = await deriveStorageKey(password, salt);
  const ad         = new TextEncoder().encode(username);

  const pm = JSON.stringify({
    identityPrivateKey: b64Encode(identity.privateKey),
    signingSecretKey:   b64Encode(identity.signingSecretKey),
    signedPreKey: {
      id:         identity.signedPreKey.id,
      publicKey:  b64Encode(identity.signedPreKey.publicKey),
      privateKey: b64Encode(identity.signedPreKey.privateKey),
    },
    oneTimePreKeys: identity.oneTimePreKeys.map(pk => ({
      id:         pk.id,
      publicKey:  b64Encode(pk.publicKey),
      privateKey: b64Encode(pk.privateKey),
    })),
  });

  const ciphertext = await crypto.subtle.encrypt(
    { name: 'AES-GCM', iv, additionalData: ad },
    storageKey,
    new TextEncoder().encode(pm),
  );

  return {
    version:         3,
    username,
    publicKeyBundle: buildPublicBundle(identity),
    privateMaterial: {
      algorithm:  'AES-256-GCM',
      ciphertext: b64Encode(new Uint8Array(ciphertext)),
      iv:         b64Encode(iv),
    },
    kdf: { algorithm: 'PBKDF2-SHA256', salt: b64Encode(salt), iterations: 600000 },
    usedOneTimePreKeyIds: [],
  };
}

async function unlockKeyFile(keyFile, password) {
  if (keyFile.version !== 3) throw new Error('Unsupported key file version');

  const salt       = b64Decode(keyFile.kdf.salt);
  const iv         = b64Decode(keyFile.privateMaterial.iv);
  const ciphertext = b64Decode(keyFile.privateMaterial.ciphertext);
  const ad         = new TextEncoder().encode(keyFile.username);
  const storageKey = await deriveStorageKey(password, salt);

  let plain;
  try {
    plain = await crypto.subtle.decrypt(
      { name: 'AES-GCM', iv, additionalData: ad },
      storageKey,
      ciphertext,
    );
  } catch {
    throw new Error('Wrong password or corrupted key file');
  }

  const pm      = JSON.parse(new TextDecoder().decode(plain));
  const usedIds = keyFile.usedOneTimePreKeyIds || [];

  return {
    publicKey:        b64Decode(keyFile.publicKeyBundle.identityKey),
    privateKey:       b64Decode(pm.identityPrivateKey),
    signingPublicKey: b64Decode(keyFile.publicKeyBundle.signingKey),
    signingSecretKey: b64Decode(pm.signingSecretKey),
    signedPreKey: {
      id:         pm.signedPreKey.id,
      publicKey:  b64Decode(pm.signedPreKey.publicKey),
      privateKey: b64Decode(pm.signedPreKey.privateKey),
    },
    oneTimePreKeys: pm.oneTimePreKeys
      .filter(pk => !usedIds.includes(pk.id))
      .map(pk => ({
        id:         pk.id,
        publicKey:  b64Decode(pk.publicKey),
        privateKey: b64Decode(pk.privateKey),
      })),
  };
}

// ─── Key storage (localStorage) ────────────────────────────────────────────

function lsKey(username) { return `kingdom:key:${username}`; }
function saveKeyFile(kf)  { localStorage.setItem(lsKey(kf.username), JSON.stringify(kf)); }
function loadKeyFile(u)   { const r = localStorage.getItem(lsKey(u)); return r ? JSON.parse(r) : null; }

function markPreKeyUsed(username, preKeyId) {
  const kf = loadKeyFile(username);
  if (!kf) return;
  if (!Array.isArray(kf.usedOneTimePreKeyIds)) kf.usedOneTimePreKeyIds = [];
  if (!kf.usedOneTimePreKeyIds.includes(preKeyId)) {
    kf.usedOneTimePreKeyIds.push(preKeyId);
    saveKeyFile(kf);
  }
}

// ─── Message encryption — matches C++ LocalKeyStore::encryptMessage ─────────

async function encryptMessage(plaintext, sender, recipientBundle, conversationId, senderId, recipientId) {
  const s = getSodium();
  validateBundle(recipientBundle);

  const recipientIkPub = b64Decode(recipientBundle.identityKey);
  const spk            = recipientBundle.signedPreKey;
  const spkPub         = b64Decode(spk.publicKey);

  let otpkId = null, otpkPub = null;
  if (Array.isArray(recipientBundle.oneTimePreKeys) && recipientBundle.oneTimePreKeys.length > 0) {
    const otpk = recipientBundle.oneTimePreKeys[0];
    otpkId  = otpk.id;
    otpkPub = b64Decode(otpk.publicKey);
  }

  // Fresh ephemeral keypair per message — zeroed after use → forward secrecy
  const ephKp = s.crypto_box_keypair();

  // DH order must match C++ LocalKeyStore::encryptMessage exactly
  const dhs = [
    dh(sender.privateKey, spkPub),           // DH1: senderIK  × recipientSPK
    dh(ephKp.privateKey, recipientIkPub),    // DH2: ephemeral × recipientIK
    dh(ephKp.privateKey, spkPub),            // DH3: ephemeral × recipientSPK
  ];
  if (otpkPub) dhs.push(dh(ephKp.privateKey, otpkPub)); // DH4: ephemeral × recipientOTPK

  const msgKey = await deriveX3dhKey(dhs);
  const nonce  = s.randombytes_buf(s.crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);

  // AD field order must match C++ associatedData() exactly
  const ad = JSON.stringify({
    version:                    1,
    algorithm:                  PAYLOAD_ALGORITHM,
    conversationId,
    senderId,
    recipientId,
    senderIdentityPublicKey:    b64Encode(sender.publicKey),
    senderEphemeralPublicKey:   b64Encode(ephKp.publicKey),
    recipientIdentityPublicKey: b64Encode(recipientIkPub),
    signedPreKeyId:             spk.id,
    oneTimePreKeyId:            otpkId,
  });

  const ciphertext = s.crypto_aead_xchacha20poly1305_ietf_encrypt(
    new TextEncoder().encode(plaintext),
    new TextEncoder().encode(ad),
    null,
    nonce,
    msgKey,
  );

  return {
    payload: JSON.stringify({
      version:                  1,
      algorithm:                PAYLOAD_ALGORITHM,
      senderIdentityPublicKey:  b64Encode(sender.publicKey),
      senderEphemeralPublicKey: b64Encode(ephKp.publicKey),
      signedPreKeyId:           spk.id,
      oneTimePreKeyId:          otpkId,
      nonce:                    b64Encode(nonce),
      ciphertext:               b64Encode(ciphertext),
    }),
    usedOneTimePreKeyId: otpkId,
  };
}

// ─── Message decryption — matches C++ LocalKeyStore::decryptMessage ─────────

async function decryptMessage(payloadStr, recipient, senderBundle, conversationId, senderId, recipientId) {
  const s = getSodium();

  let payload;
  try { payload = JSON.parse(payloadStr); } catch { return null; }
  if (payload.version !== 1 || payload.algorithm !== PAYLOAD_ALGORITHM) return null;

  try { validateBundle(senderBundle); } catch { return null; }

  if (payload.senderIdentityPublicKey !== senderBundle.identityKey) return null;

  const senderIkPub = b64Decode(payload.senderIdentityPublicKey);
  const ephPub      = b64Decode(payload.senderEphemeralPublicKey);

  if (recipient.signedPreKey.id !== payload.signedPreKeyId) return null;

  const otpkId = payload.oneTimePreKeyId;
  let otpkPriv = null;
  if (otpkId !== null && otpkId !== undefined) {
    const otpk = recipient.oneTimePreKeys.find(pk => pk.id === otpkId);
    if (!otpk) return null;
    otpkPriv = otpk.privateKey;
  }

  // DH order must match C++ LocalKeyStore::decryptMessage exactly
  const dhs = [
    dh(recipient.signedPreKey.privateKey, senderIkPub), // DH1: recipientSPK × senderIK
    dh(recipient.privateKey, ephPub),                   // DH2: recipientIK  × ephemeral
    dh(recipient.signedPreKey.privateKey, ephPub),      // DH3: recipientSPK × ephemeral
  ];
  if (otpkPriv) dhs.push(dh(otpkPriv, ephPub));         // DH4: recipientOTPK × ephemeral

  const msgKey = await deriveX3dhKey(dhs);

  const nonce      = b64Decode(payload.nonce);
  const ciphertext = b64Decode(payload.ciphertext);

  // AD must match the encrypt side field order exactly
  const ad = JSON.stringify({
    version:                    1,
    algorithm:                  PAYLOAD_ALGORITHM,
    conversationId,
    senderId,
    recipientId,
    senderIdentityPublicKey:    payload.senderIdentityPublicKey,
    senderEphemeralPublicKey:   payload.senderEphemeralPublicKey,
    recipientIdentityPublicKey: b64Encode(recipient.publicKey),
    signedPreKeyId:             payload.signedPreKeyId,
    oneTimePreKeyId:            otpkId !== undefined ? otpkId : null,
  });

  try {
    const plain = s.crypto_aead_xchacha20poly1305_ietf_decrypt(
      null,
      ciphertext,
      new TextEncoder().encode(ad),
      nonce,
      msgKey,
    );
    return new TextDecoder().decode(plain);
  } catch {
    return null;
  }
}

// ─── API ───────────────────────────────────────────────────────────────────

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

// ─── Bundle cache ──────────────────────────────────────────────────────────

async function getBundle(userId) {
  if (state.bundleCache[userId]) return state.bundleCache[userId];
  const data = await api('GET', `/users/${userId}/public-key`);
  if (!data.publicKey) throw new Error(`User ${userId} has no public key`);

  let bundle;
  try { bundle = JSON.parse(data.publicKey); } catch {
    throw new Error(`User ${userId} public key is not a valid X3DH bundle`);
  }
  validateBundle(bundle);
  state.bundleCache[userId] = bundle;
  return bundle;
}

// ─── Auth ──────────────────────────────────────────────────────────────────

async function doSignup(username, password) {
  await sodium.ready;
  const identity = generateIdentity();
  const bundle   = buildPublicBundle(identity);

  const result = await api('POST', '/signup', {
    username,
    password,
    publicKey: JSON.stringify(bundle),
  });

  const kf = await buildKeyFile(username, identity, password);
  saveKeyFile(kf);
  return { result, identity };
}

async function doLogin(username, password) {
  await sodium.ready;
  const result = await api('POST', '/login', { username, password });
  const kf = loadKeyFile(username);
  if (!kf) throw new Error('No local key file found. Did you sign up on this device?');
  const identity = await unlockKeyFile(kf, password);
  return { result, identity };
}

function startSession(result, identity, username) {
  state.token    = result.token || result.sessionToken;
  state.userId   = result.id;
  state.username = username;
  state.identity = identity;
  state.bundleCache[result.id] = buildPublicBundle(identity);
}

async function doLogout() {
  try { await api('POST', '/logout'); } catch { /* ignore */ }
  clearSession();
}

function clearSession() {
  if (state.pollInterval) clearInterval(state.pollInterval);
  Object.assign(state, {
    token: null, userId: null, username: null, identity: null,
    conversations: [], activeConvId: null, messages: [],
    bundleCache: {}, plaintextCache: {}, pollInterval: null,
  });
  showView('auth');
}

// ─── Conversations ─────────────────────────────────────────────────────────

async function loadConversations() {
  const data = await api('GET', `/users/${state.userId}/conversations`);
  state.conversations = Array.isArray(data) ? data : [];
  renderConversations();
}

async function createConversation(name, participantIds) {
  const ids = [...new Set([...participantIds, state.userId])];
  return api('POST', '/conversations', { name, participantIds: ids });
}

// ─── Messages ──────────────────────────────────────────────────────────────

async function loadMessages(convId) {
  const data = await api('GET', `/conversations/${convId}/messages`);
  state.messages = Array.isArray(data) ? data : [];
  state.messages.sort((a, b) => a.timestamp - b.timestamp);

  const senderIds = [...new Set(
    state.messages.map(m => m.senderId).filter(id => id !== state.userId),
  )];
  await Promise.allSettled(senderIds.map(id => getBundle(id)));

  await renderMessages();
}

async function sendMessage(convId, text, recipientId) {
  if (!state.identity) throw new Error('Not logged in');

  const recipientBundle = await getBundle(recipientId);
  const { payload, usedOneTimePreKeyId } = await encryptMessage(
    text, state.identity, recipientBundle, convId, state.userId, recipientId,
  );

  const msg = await api('POST', `/conversations/${convId}/messages`, {
    senderId: state.userId,
    payload,
  });

  state.plaintextCache[msg.id] = text;

  if (usedOneTimePreKeyId !== null) {
    api('POST', `/users/${recipientId}/one-time-prekeys/${usedOneTimePreKeyId}/consume`, {})
      .catch(() => {});
    markPreKeyUsed(state.username, usedOneTimePreKeyId);
  }

  return msg;
}

// ─── Rendering ─────────────────────────────────────────────────────────────

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
    const others = (conv.participantIds || []).filter(id => id !== state.userId);
    const meta = others.length === 0 ? 'Just you' : `${conv.participantIds.length} participants`;
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
  renderConversations();

  document.getElementById('chat-empty').classList.add('hidden');
  document.getElementById('chat-active').classList.remove('hidden');
  document.getElementById('chat-name').textContent = conv.name;
  document.getElementById('chat-participants').textContent =
    `Participants: ${(conv.participantIds || []).join(', ')}`;

  const others          = (conv.participantIds || []).filter(id => id !== state.userId);
  const recipientSelect = document.getElementById('recipient-select');
  const recipientRow    = document.getElementById('recipient-row');

  if (others.length === 1) {
    recipientSelect.innerHTML = `<option value="${others[0]}">User ${others[0]}</option>`;
    recipientRow.classList.add('hidden');
  } else if (others.length > 1) {
    recipientSelect.innerHTML = others.map(id => `<option value="${id}">User ${id}</option>`).join('');
    recipientRow.classList.remove('hidden');
  } else {
    recipientSelect.innerHTML = `<option value="${state.userId}">Yourself</option>`;
    recipientRow.classList.add('hidden');
  }

  await loadMessages(conv.id);

  if (state.pollInterval) clearInterval(state.pollInterval);
  state.pollInterval = setInterval(() => {
    if (state.activeConvId === conv.id) loadMessages(conv.id).catch(() => {});
  }, 5000);
}

async function renderMessages() {
  const container = document.getElementById('messages');
  const atBottom  = container.scrollHeight - container.scrollTop - container.clientHeight < 60;

  const rendered = await Promise.all(state.messages.map(async msg => {
    const isOwn = msg.senderId === state.userId;

    if (isOwn) {
      return { msg, isOwn, text: state.plaintextCache[msg.id] || null };
    }

    const senderBundle = state.bundleCache[msg.senderId];
    if (!senderBundle || !state.identity) return { msg, isOwn, text: null };

    const text = await decryptMessage(
      msg.payload, state.identity, senderBundle,
      msg.conversationId, msg.senderId, state.userId,
    );
    return { msg, isOwn, text };
  }));

  container.innerHTML = '';
  for (const { msg, isOwn, text } of rendered) {
    const div       = document.createElement('div');
    div.className   = `message ${isOwn ? 'own' : 'other'}`;
    const sender    = isOwn ? 'You' : `User ${msg.senderId}`;
    const time      = new Date(msg.timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    const chainHtml = msg.blockchainDigest
      ? `<span class="chain-badge" title="Sepolia: ${escHtml(msg.blockchainDigest)}">&#9744; on-chain</span>`
      : '';
    const bodyHtml = text !== null
      ? escHtml(text)
      : (isOwn ? '&#128274; [sent — plaintext not cached]' : '&#128274; [encrypted]');

    div.innerHTML = `
      <div class="message-sender">${escHtml(sender)}</div>
      <div class="message-bubble${text === null ? ' failed' : ''}">${bodyHtml}</div>
      <div class="message-footer"><span>${time}</span>${chainHtml}</div>
    `;
    container.appendChild(div);
  }

  if (atBottom || state.messages.length <= 5) container.scrollTop = container.scrollHeight;
}

// ─── Views ─────────────────────────────────────────────────────────────────

function showView(view) {
  document.getElementById('view-auth').classList.toggle('hidden', view !== 'auth');
  document.getElementById('view-app').classList.toggle('hidden', view !== 'app');
}

function showAuthError(msg) {
  const el = document.getElementById('auth-error');
  el.textContent = msg;
  el.classList.remove('hidden');
}

function clearAuthError() { document.getElementById('auth-error').classList.add('hidden'); }

function showModalError(msg) {
  const el = document.getElementById('modal-error');
  el.textContent = msg;
  el.classList.remove('hidden');
}

function clearModalError() { document.getElementById('modal-error').classList.add('hidden'); }

function showApp() {
  document.getElementById('sidebar-username').textContent = state.username;
  document.getElementById('sidebar-userid').textContent   = `#${state.userId}`;
  document.getElementById('chat-empty').classList.remove('hidden');
  document.getElementById('chat-active').classList.add('hidden');
  showView('app');
  loadConversations().catch(err => console.error('Failed to load conversations:', err));
}

// ─── Utility ───────────────────────────────────────────────────────────────

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

// ─── Event wiring ──────────────────────────────────────────────────────────

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
  document.getElementById('logout-btn').addEventListener('click', () => doLogout());

  // New conversation
  document.getElementById('new-conv-btn').addEventListener('click', () => {
    clearModalError();
    document.getElementById('conv-name-input').value       = '';
    document.getElementById('participant-ids-input').value = '';
    document.getElementById('modal-overlay').classList.remove('hidden');
  });

  // Modal cancel / overlay click to close
  document.getElementById('modal-cancel').addEventListener('click', () => {
    document.getElementById('modal-overlay').classList.add('hidden');
  });
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

    const raw            = document.getElementById('participant-ids-input').value;
    const participantIds = raw.split(',')
      .map(s => parseInt(s.trim(), 10))
      .filter(n => !isNaN(n));

    try {
      await createConversation(name, participantIds);
      document.getElementById('modal-overlay').classList.add('hidden');
      await loadConversations();
    } catch (err) {
      showModalError(err.message);
    }
  });

  // Send message
  document.getElementById('send-btn').addEventListener('click', sendCurrentMessage);
  document.getElementById('message-input').addEventListener('keydown', e => {
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendCurrentMessage(); }
  });
});

async function sendCurrentMessage() {
  const input = document.getElementById('message-input');
  const text  = input.value.trim();
  if (!text || !state.activeConvId) return;

  const recipientId = parseInt(document.getElementById('recipient-select').value, 10);
  if (isNaN(recipientId)) return;

  const btn = document.getElementById('send-btn');
  btn.disabled   = true;
  input.disabled = true;

  try {
    await sendMessage(state.activeConvId, text, recipientId);
    input.value = '';
    await loadMessages(state.activeConvId);
  } catch (err) {
    alert(`Failed to send: ${err.message}`);
  } finally {
    btn.disabled   = false;
    input.disabled = false;
    input.focus();
  }
}
