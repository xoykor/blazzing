#!/usr/bin/env bash
set -euo pipefail
PORT=18787
BASE="http://127.0.0.1:${PORT}"
ID="0123456789abcdef0123456789abcdef"
LOG="/tmp/blazzing-worker-test.log"

npx wrangler dev --ip 127.0.0.1 --port "${PORT}" >"${LOG}" 2>&1 &
PID=$!
trap 'kill "${PID}" 2>/dev/null || true' EXIT

for _ in $(seq 1 60); do
  if curl --fail --silent --output /dev/null "${BASE}/healthz"; then break; fi
  sleep 0.25
done
curl --fail --silent --output /dev/null "${BASE}/healthz" || { cat "${LOG}"; exit 1; }

status() { curl --silent --output /tmp/blazzing-worker-body --write-out '%{http_code}' "$@"; }

[[ "$(status -X POST "${BASE}/api/v1/sessions/${ID}")" == "201" ]]
[[ "$(status "${BASE}/pair/${ID}")" == "200" ]]
grep -q "Adicionar playlist ao Blazzing" /tmp/blazzing-worker-body
[[ "$(status "${BASE}/pair.js")" == "200" ]]
grep -q "AES-GCM" /tmp/blazzing-worker-body
[[ "$(status "${BASE}/api/v1/sessions/${ID}/payload")" == "204" ]]
PAYLOAD='{"iv":"abcdefghijklmnop","ciphertext":"abcdefghijklmnopqrstuvwxyz"}'
[[ "$(status -X POST -H 'Content-Type: application/json' --data "${PAYLOAD}" "${BASE}/api/v1/sessions/${ID}/payload")" == "204" ]]
[[ "$(status "${BASE}/api/v1/sessions/${ID}/payload")" == "200" ]]
grep -q '"ciphertext"' /tmp/blazzing-worker-body
[[ "$(status -X DELETE "${BASE}/api/v1/sessions/${ID}")" == "204" ]]
[[ "$(status "${BASE}/api/v1/sessions/${ID}/payload")" == "410" ]]
[[ "$(status -I "${BASE}/healthz")" == "204" ]]
echo "Worker pairing smoke test: OK"
