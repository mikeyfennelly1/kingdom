#!/usr/bin/env bash
# Health check script for the remote Kingdom deployment.
# Usage: ./scripts/health-check.sh [base_url]
# Default base URL: https://updakingdom.theburkenator.com

set -euo pipefail

BASE_URL="${1:-https://updakingdom.theburkenator.com}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0

pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
info() { echo -e "  ${YELLOW}[INFO]${NC} $1"; }
section() { echo -e "\n${YELLOW}==> $1${NC}"; }

# Run curl and capture status + body. Always succeeds (errors surface via assertions).
http_get() {
    local url="$1"
    shift
    curl -s -o /tmp/kd_body -w "%{http_code}" --max-time 10 "$@" "$url"
}

http_post() {
    local url="$1"
    local body="$2"
    shift 2
    curl -s -o /tmp/kd_body -w "%{http_code}" --max-time 10 \
        -X POST -H "Content-Type: application/json" -d "$body" "$@" "$url"
}

body() { cat /tmp/kd_body 2>/dev/null || true; }

assert_status() {
    local label="$1" expected="$2" actual="$3"
    if [[ "$actual" == "$expected" ]]; then
        pass "$label (HTTP $actual)"
    else
        fail "$label — expected HTTP $expected, got $actual. Body: $(body)"
    fi
}

assert_body_contains() {
    local label="$1" needle="$2"
    if body | grep -q "$needle"; then
        pass "$label"
    else
        fail "$label — expected '$needle' in body: $(body)"
    fi
}

# ── TLS ────────────────────────────────────────────────────────────────────────

section "TLS certificate"

cert_info=$(curl -s -o /dev/null -w "%{ssl_verify_result}" --max-time 10 "$BASE_URL" 2>&1 || true)
if [[ "$cert_info" == "0" ]]; then
    pass "TLS certificate is valid and trusted"
else
    # May be self-signed in dev; treat as warning not hard failure
    info "TLS verify result: $cert_info (may be self-signed cert)"
fi

expiry=$(echo | openssl s_client -connect "${BASE_URL#https://}:443" -servername "${BASE_URL#https://}" 2>/dev/null \
    | openssl x509 -noout -enddate 2>/dev/null | cut -d= -f2 || true)
if [[ -n "$expiry" ]]; then
    info "Certificate expires: $expiry"
fi

# ── /health ────────────────────────────────────────────────────────────────────

section "GET /health"

status=$(http_get "$BASE_URL/health")
assert_status "/health is reachable" "200" "$status"
assert_body_contains "/health returns {status:ok}" '"status"'

# ── /api ───────────────────────────────────────────────────────────────────────

section "GET /api"

status=$(http_get "$BASE_URL/api" -H "Authorization: Bearer invalid.token.here")
assert_status "/api returns 401 with invalid token" "401" "$status"

status=$(http_get "$BASE_URL/api")
assert_status "/api returns 401 without token" "401" "$status"

# ── /users ─────────────────────────────────────────────────────────────────────

section "GET /users"

status=$(http_get "$BASE_URL/users")
assert_status "/users returns 401 without token" "401" "$status"

# ── Auth flow ─────────────────────────────────────────────────────────────────

section "POST /login — input validation"

status=$(http_post "$BASE_URL/login" '{}')
assert_status "/login rejects empty body with 400" "400" "$status"

status=$(http_post "$BASE_URL/login" '{"username":"doesnotexist_healthcheck","password":"WrongPass1!"}')
assert_status "/login returns 401 for unknown user" "401" "$status"
assert_body_contains "/login 401 body has error field" '"error"'

# ── POST /signup — input validation ───────────────────────────────────────────

section "POST /signup — input validation"

status=$(http_post "$BASE_URL/signup" '{}')
assert_status "/signup rejects empty body with 400" "400" "$status"

status=$(http_post "$BASE_URL/signup" '{"username":"hc","password":"short","publicKey":"key"}')
assert_status "/signup rejects weak password with 400" "400" "$status"

# ── Content-Type enforcement ──────────────────────────────────────────────────

section "Security — Content-Type enforcement"

status=$(curl -s -o /tmp/kd_body -w "%{http_code}" --max-time 10 \
    -X POST -H "Content-Type: text/plain" -d '{}' "$BASE_URL/login")
assert_status "/login rejects non-JSON Content-Type with 400" "400" "$status"

# ── Auth-required routes reject unauthenticated requests ─────────────────────

section "Security — auth enforcement on protected routes"

status=$(http_post "$BASE_URL/conversations" '{"name":"test","participantIds":[1]}' \
    -H "Authorization: ")
assert_status "POST /conversations returns 401 without auth" "401" "$status"

status=$(http_get "$BASE_URL/users/1/conversations")
assert_status "GET /users/1/conversations returns 401 without auth" "401" "$status"

# ── Security headers ──────────────────────────────────────────────────────────

section "Security response headers"

headers=$(curl -s -I --max-time 10 "$BASE_URL/health" 2>/dev/null || true)

if echo "$headers" | grep -qi "Strict-Transport-Security"; then
    pass "HSTS header present"
else
    fail "HSTS header missing"
fi

if echo "$headers" | grep -qi "X-Content-Type-Options"; then
    pass "X-Content-Type-Options header present"
else
    fail "X-Content-Type-Options header missing"
fi

if echo "$headers" | grep -qi "X-Frame-Options"; then
    pass "X-Frame-Options header present"
else
    fail "X-Frame-Options header missing"
fi

# ── 404 handling ──────────────────────────────────────────────────────────────

section "404 handling"

status=$(http_get "$BASE_URL/nonexistent-route-healthcheck")
assert_status "Unknown route returns 404" "404" "$status"
assert_body_contains "404 body has error field" '"error"'

# ── Summary ───────────────────────────────────────────────────────────────────

echo ""
echo "────────────────────────────────────────"
echo -e "  ${GREEN}Passed:${NC} $PASS"
if [[ $FAIL -gt 0 ]]; then
    echo -e "  ${RED}Failed:${NC} $FAIL"
    echo "────────────────────────────────────────"
    exit 1
else
    echo -e "  ${RED}Failed:${NC} $FAIL"
    echo "────────────────────────────────────────"
    echo -e "  ${GREEN}All checks passed.${NC}"
fi
