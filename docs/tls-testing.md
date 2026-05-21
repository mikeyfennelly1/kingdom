# TLS Manual Testing Commands

## Prerequisites

Two terminal tabs open, both in the kingdom directory inside devbox shell.

```bash
cd ~/EPIC/kingdom
devbox shell
```

```bash
docker compose up -d
```

---

## Terminal 1 — Run the server

```bash
KD_DB_URL="postgresql://kingdom:kingdom@localhost:5432/kingdom" KD_TLS_CERT=certs/server.crt KD_TLS_KEY=certs/server.key ./build/kds/kds
```

---

## Terminal 2 — Run the tests

### TLS enforcement

```bash
curl --cacert certs/server.crt https://localhost:8080/health
```

Expected: `{"status":"ok"}`

```bash
curl https://localhost:8080/health
```

Expected: `curl: (60) SSL certificate problem: self signed certificate`

```bash
openssl req -x509 -newkey rsa:2048 -days 1 -nodes -keyout /tmp/wrong.key -out /tmp/wrong.crt -subj "/CN=wrong"
curl --cacert /tmp/wrong.crt https://localhost:8080/health
```

Expected: `curl: (60) SSL certificate problem: self signed certificate`

---

### TLS handshake details

```bash
curl -v --cacert certs/server.crt https://localhost:8080/health 2>&1 | grep -E "SSL connection|subject|issuer|subjectAltName|verify"
```

Expected output includes:
- `SSL connection using TLSv1.3`
- `subject: CN=localhost`
- `subjectAltName: host "localhost" matched cert's "localhost"`
- `SSL certificate verify ok`

---

### Auth flow over HTTPS

```bash
curl --cacert certs/server.crt -X POST https://localhost:8080/signup -H "Content-Type: application/json" -d '{"username":"alice","password":"hunter2"}'
```

Expected: `{"id":<n>,"sessionToken":"<64 hex chars>","username":"alice"}`

```bash
curl --cacert certs/server.crt -X POST https://localhost:8080/login -H "Content-Type: application/json" -d '{"username":"alice","password":"hunter2"}'
```

Expected: `{"id":<n>,"sessionToken":"<64 hex chars>","username":"alice"}`

```bash
curl --cacert certs/server.crt -X POST https://localhost:8080/login -H "Content-Type: application/json" -d '{"username":"alice","password":"wrongpassword"}'
```

Expected: `{"error":"invalid username or password"}` with HTTP 401

---

### Session token entropy check

The sessionToken in signup/login responses should be exactly 64 hex characters (256 bits of CSPRNG output). Count the characters:

```bash
curl --cacert certs/server.crt -X POST https://localhost:8080/signup -H "Content-Type: application/json" -d '{"username":"bob","password":"hunter2"}' | python3 -c "import sys,json; t=json.load(sys.stdin)['sessionToken']; print(f'Token: {t}'); print(f'Length: {len(t)}')"
```

Expected: `Length: 64`

---

### kdctl interactive shell over HTTPS

```bash
KD_PROTOCOL=https KD_CA_CERT=certs/server.crt ./build/kdctl/kdctl
```

Then inside the shell:

```
signup
login
create-conversation
send
messages
exit
```

---

### kdctl subcommands over HTTPS

```bash
KD_PROTOCOL=https KD_CA_CERT=certs/server.crt ./build/kdctl/kdctl health
```

Expected: `{"status":"ok"}`
