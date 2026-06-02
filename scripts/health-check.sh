#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ_ROOT="${SCRIPT_DIR}/.."

RAW_HOST="${KD_RAW_HOST:-200.69.13.70}"
RAW_PORT="${KD_RAW_PORT:-4000}"
DNS_HOST="${KD_DNS_HOST:-updakingdom.theburkenator.com}"
CA_CERT="${PROJ_ROOT}/certs/server.crt"

RAW_URL="https://${RAW_HOST}:${RAW_PORT}"
DNS_URL="https://${DNS_HOST}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0
BASE_URL=""
CURL_OPTS=()

pass()    { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail()    { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
info()    { echo -e "  ${YELLOW}[INFO]${NC} $1"; }
section() { echo -e "\n${YELLOW}==> $1${NC}"; }

http_get() {
    local url="$1"; shift
    curl -s -o /tmp/kd_body -w "%{http_code}" --max-time 10 \
        "${CURL_OPTS[@]+"${CURL_OPTS[@]}"}" "$@" "$url"
}

http_post() {
    local url="$1" body="$2"; shift 2
    curl -s -o /tmp/kd_body -w "%{http_code}" --max-time 10 \
        -X POST -H "Content-Type: application/json" -d "$body" \
        "${CURL_OPTS[@]+"${CURL_OPTS[@]}"}" "$@" "$url"
}

body() { cat /tmp/kd_body 2>/dev/null || true; }

assert_status() {
    local label="$1" expected="$2" actual="$3"
    if [[ "$actual" == "$expected" ]]; then
        pass "$label (HTTP $actual)"
    else
        fail "$label — expected HTTP $expected, got HTTP $actual. Body: $(body)"
    fi
}

assert_body_contains() {
    local label="$1" needle="$2"
    if body | grep -q "$needle"; then
        pass "$label"
    else
        fail "$label — expected '${needle}' in body: $(body)"
    fi
}

probe_health() {
    local url="$1"; shift
    local status
    status=$(curl -s -o /tmp/kd_probe -w "%{http_code}" --max-time 10 "$@" "${url}/health" 2>/dev/null || echo "000")
    [[ "$status" == "200" ]] && grep -q '"ok"' /tmp/kd_probe 2>/dev/null
}

select_target() {
    local raw_ok=false dns_ok=false

    section "Pre-flight: raw IP ${RAW_URL}/health  (--cacert)"
    if probe_health "${RAW_URL}" --cacert "${CA_CERT}"; then
        pass "Server reachable at ${RAW_URL}"
        raw_ok=true
    else
        fail "Server unreachable at ${RAW_URL}"
    fi

    section "Pre-flight: DNS ${DNS_URL}/health"
    if probe_health "${DNS_URL}"; then
        pass "Server reachable at ${DNS_URL}"
        dns_ok=true
    else
        fail "Server unreachable at ${DNS_URL}"
    fi

    if [[ "${raw_ok}" == false && "${dns_ok}" == false ]]; then
        echo -e "\n${RED}Server is not reachable on either target — aborting.${NC}"
        exit 1
    fi

    if [[ "${dns_ok}" == true ]]; then
        BASE_URL="${DNS_URL}"
        CURL_OPTS=()
        info "Running suite against DNS target: ${BASE_URL}"
    else
        BASE_URL="${RAW_URL}"
        CURL_OPTS=(--cacert "${CA_CERT}")
        info "DNS unreachable — running suite against raw IP: ${BASE_URL}"
    fi
}

run_suite() {
    local status headers

    # ── TLS ──────────────────────────────────────────────────────────────────

    section "TLS certificate"

    local verify_result
    verify_result=$(curl -s -o /dev/null -w "%{ssl_verify_result}" --max-time 10 \
        "${CURL_OPTS[@]+"${CURL_OPTS[@]}"}" "${BASE_URL}" 2>&1 || true)
    if [[ "${verify_result}" == "0" ]]; then
        pass "TLS certificate valid"
    else
        info "TLS verify result: ${verify_result} (self-signed cert in use)"
    fi

    if [[ "${BASE_URL}" == "${DNS_URL}" ]]; then
        local expiry
        expiry=$(echo | openssl s_client -connect "${DNS_HOST}:443" -servername "${DNS_HOST}" 2>/dev/null \
            | openssl x509 -noout -enddate 2>/dev/null | cut -d= -f2 || true)
        [[ -n "${expiry}" ]] && info "Certificate expires: ${expiry}"
    fi

    # ── /health ───────────────────────────────────────────────────────────────

    section "GET /health"

    status=$(http_get "${BASE_URL}/health")
    assert_status "/health reachable" "200" "${status}"
    assert_body_contains "/health returns {status:ok}" '"status"'

    # ── /api ──────────────────────────────────────────────────────────────────

    section "GET /api"

    status=$(http_get "${BASE_URL}/api" -H "Authorization: Bearer invalid.token.here")
    assert_status "/api rejects invalid token with 401" "401" "${status}"

    status=$(http_get "${BASE_URL}/api")
    assert_status "/api rejects missing token with 401" "401" "${status}"

    # ── /users ────────────────────────────────────────────────────────────────

    section "GET /users"

    status=$(http_get "${BASE_URL}/users")
    assert_status "/users returns 401 without auth" "401" "${status}"

    # ── POST /login — input validation ────────────────────────────────────────

    section "POST /login — input validation"

    status=$(http_post "${BASE_URL}/login" '{}')
    assert_status "/login rejects empty body with 400" "400" "${status}"

    status=$(http_post "${BASE_URL}/login" '{"username":"doesnotexist_healthcheck","password":"WrongPass1!"}')
    assert_status "/login returns 401 for unknown user" "401" "${status}"
    assert_body_contains "/login 401 body has error field" '"error"'

    # ── POST /signup — input validation ───────────────────────────────────────

    section "POST /signup — input validation"

    status=$(http_post "${BASE_URL}/signup" '{}')
    assert_status "/signup rejects empty body with 400" "400" "${status}"

    status=$(http_post "${BASE_URL}/signup" '{"username":"hc","password":"short","publicKey":"key"}')
    assert_status "/signup rejects weak password with 400" "400" "${status}"

    # ── Content-Type enforcement ──────────────────────────────────────────────

    section "Security — Content-Type enforcement"

    status=$(curl -s -o /tmp/kd_body -w "%{http_code}" --max-time 10 \
        "${CURL_OPTS[@]+"${CURL_OPTS[@]}"}" \
        -X POST -H "Content-Type: text/plain" -d '{}' "${BASE_URL}/login")
    assert_status "/login rejects non-JSON Content-Type with 400" "400" "${status}"

    # ── Auth enforcement ──────────────────────────────────────────────────────

    section "Security — auth enforcement on protected routes"

    status=$(http_post "${BASE_URL}/conversations" '{"name":"test","participantIds":[1]}' \
        -H "Authorization: ")
    assert_status "POST /conversations returns 401 without auth" "401" "${status}"

    status=$(http_get "${BASE_URL}/users/1/conversations")
    assert_status "GET /users/1/conversations returns 401 without auth" "401" "${status}"

    # ── Security headers ──────────────────────────────────────────────────────

    section "Security response headers"

    headers=$(curl -s -I --max-time 10 "${CURL_OPTS[@]+"${CURL_OPTS[@]}"}" "${BASE_URL}/health" 2>/dev/null || true)

    echo "${headers}" | grep -qi "Strict-Transport-Security" \
        && pass "HSTS header present" || fail "HSTS header missing"
    echo "${headers}" | grep -qi "X-Content-Type-Options" \
        && pass "X-Content-Type-Options header present" || fail "X-Content-Type-Options header missing"
    echo "${headers}" | grep -qi "X-Frame-Options" \
        && pass "X-Frame-Options header present" || fail "X-Frame-Options header missing"

    # ── 404 handling ──────────────────────────────────────────────────────────

    section "404 handling"

    status=$(http_get "${BASE_URL}/nonexistent-route-healthcheck")
    assert_status "Unknown route returns 404" "404" "${status}"
    assert_body_contains "404 body has error field" '"error"'
}

print_summary() {
    echo ""
    echo "────────────────────────────────────────"
    echo -e "  ${GREEN}Passed:${NC} ${PASS}"
    if [[ "${FAIL}" -gt 0 ]]; then
        echo -e "  ${RED}Failed:${NC} ${FAIL}"
        echo "────────────────────────────────────────"
        exit 1
    else
        echo -e "  ${RED}Failed:${NC} ${FAIL}"
        echo "────────────────────────────────────────"
        echo -e "  ${GREEN}All checks passed.${NC}"
    fi
}

main() {
    select_target
    run_suite
    print_summary
}

main "$@"
