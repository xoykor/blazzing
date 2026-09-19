#!/usr/bin/env bash
set -euo pipefail
PORT=18787
BASE="http://127.0.0.1:${PORT}"
ID="0123456789abcdef0123456789abcdef"
LOG="/tmp/blazzing-worker-test.log"
BODY="/tmp/blazzing-worker-body"

npx wrangler dev --ip 127.0.0.1 --port "${PORT}" >"${LOG}" 2>&1 &
PID=$!
trap 'kill "${PID}" 2>/dev/null || true' EXIT

for _ in $(seq 1 60); do
  if curl --fail --silent --output /dev/null "${BASE}/healthz"; then break; fi
  sleep 0.25
done
curl --fail --silent --output /dev/null "${BASE}/healthz" || { cat "${LOG}"; exit 1; }

request_status() {
  curl --silent --output "${BODY}" --write-out '%{http_code}' "$@"
}

expect_status() {
  local expected="$1"
  local label="$2"
  shift 2
  local got
  got="$(request_status "$@")"
  if [[ "${got}" != "${expected}" ]]; then
    echo "${label}: expected HTTP ${expected}, got ${got}" >&2
    cat "${BODY}" >&2 || true
    echo >&2
    cat "${LOG}" >&2 || true
    exit 1
  fi
}

expect_status 204 cors-preflight -X OPTIONS -H "Origin: app://blazzing" -H "Access-Control-Request-Method: POST" "${BASE}/api/v1/sessions/${ID}"
expect_status 201 create -X POST -H "Origin: app://blazzing" "${BASE}/api/v1/sessions/${ID}"
expect_status 200 pair-page "${BASE}/pair/${ID}"
grep -q "Adicionar playlist ao Blazzing" "${BODY}"
expect_status 200 pair-js "${BASE}/pair.js"
grep -q "AES-GCM" "${BODY}"
expect_status 204 empty-poll -H "Origin: app://blazzing" "${BASE}/api/v1/sessions/${ID}/payload"
grep -qi "^$" "${BODY}" || true

PAYLOAD='{"iv":"abcdefghijklmnop","ciphertext":"abcdefghijklmnopqrstuvwxyz"}'
expect_status 204 submit -X POST -H 'Content-Type: application/json' --data "${PAYLOAD}" "${BASE}/api/v1/sessions/${ID}/payload"
expect_status 200 poll "${BASE}/api/v1/sessions/${ID}/payload"
grep -q '"ciphertext"' "${BODY}"
expect_status 204 delete -X DELETE "${BASE}/api/v1/sessions/${ID}"
expect_status 410 deleted-poll "${BASE}/api/v1/sessions/${ID}/payload"
expect_status 204 health-head -I "${BASE}/healthz"

HEADERS="/tmp/blazzing-worker-headers"
curl --silent --dump-header "${HEADERS}" --output /dev/null -H "Origin: app://blazzing" "${BASE}/api/v1/sessions/${ID}/payload" || true
grep -qi "^Access-Control-Allow-Origin: \*" "${HEADERS}"

echo "Worker pairing smoke test: OK"
