#!/usr/bin/env bash
set -euo pipefail

# One-command installer for an Oracle Cloud Ubuntu 24.04 instance.
# Run from a Blazzing checkout:
#   sudo bash relay/deploy/install-oracle-ubuntu.sh
#
# An explicit public IPv4 can be supplied as the first argument. Otherwise the
# script discovers the VM's outbound IPv4 once during installation.

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run this installer with sudo/root." >&2
  exit 1
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
RELAY_DIR="${REPO_ROOT}/relay"

if [[ ! -f "${RELAY_DIR}/go.mod" ]]; then
  echo "Run this script from a complete Blazzing repository checkout." >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y ca-certificates curl git gnupg debian-keyring debian-archive-keyring apt-transport-https golang-go

# Install Caddy from its official Debian/Ubuntu repository.
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' |
  gpg --dearmor --yes -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt'   > /etc/apt/sources.list.d/caddy-stable.list
chmod o+r /usr/share/keyrings/caddy-stable-archive-keyring.gpg
chmod o+r /etc/apt/sources.list.d/caddy-stable.list
apt-get update
apt-get install -y caddy

GO_VERSION="$(go env GOVERSION | sed 's/^go//')"
GO_MAJOR="${GO_VERSION%%.*}"
GO_REST="${GO_VERSION#*.}"
GO_MINOR="${GO_REST%%.*}"
if (( GO_MAJOR < 1 || (GO_MAJOR == 1 && GO_MINOR < 22) )); then
  echo "Go 1.22+ is required; found ${GO_VERSION}." >&2
  exit 1
fi

PUBLIC_IP="${1:-}"
if [[ -z "${PUBLIC_IP}" ]]; then
  PUBLIC_IP="$(curl --fail --silent --show-error --ipv4 https://api.ipify.org)"
fi
if [[ ! "${PUBLIC_IP}" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]; then
  echo "Could not determine a valid public IPv4: ${PUBLIC_IP}" >&2
  exit 1
fi

IFS=. read -r A B C D <<< "${PUBLIC_IP}"
for OCTET in "${A}" "${B}" "${C}" "${D}"; do
  if (( OCTET < 0 || OCTET > 255 )); then
    echo "Invalid public IPv4: ${PUBLIC_IP}" >&2
    exit 1
  fi
done

PAIR_HOST="${PUBLIC_IP//./-}.sslip.io"
PAIR_URL="https://${PAIR_HOST}"

if ! id blazzing >/dev/null 2>&1; then
  useradd --system --no-create-home --shell /usr/sbin/nologin blazzing
fi

install -d -o root -g root -m 0755 /opt/blazzing-pairing
(
  cd "${RELAY_DIR}"
  go test ./...
  CGO_ENABLED=0 go build -trimpath -ldflags="-s -w"     -o /tmp/blazzing-pairing-relay .
)
install -o root -g root -m 0755 /tmp/blazzing-pairing-relay   /opt/blazzing-pairing/blazzing-pairing-relay
install -o root -g root -m 0644 "${SCRIPT_DIR}/blazzing-pairing.service"   /etc/systemd/system/blazzing-pairing.service

cat > /etc/caddy/Caddyfile <<EOF
${PAIR_HOST} {
    encode zstd gzip

    reverse_proxy 127.0.0.1:8080 {
        header_up X-Real-IP {remote_host}
    }

    header {
        Strict-Transport-Security "max-age=31536000"
    }
}
EOF

caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile
systemctl daemon-reload
systemctl enable --now blazzing-pairing.service
systemctl enable caddy.service
systemctl restart caddy.service

if command -v ufw >/dev/null 2>&1 && ufw status | grep -q '^Status: active'; then
  ufw allow 80/tcp
  ufw allow 443/tcp
fi

curl --fail --silent --show-error --output /dev/null http://127.0.0.1:8080/healthz

echo
echo "Blazzing pairing relay installed."
echo "Public relay URL: ${PAIR_URL}"
echo
echo "Oracle Cloud must allow inbound TCP 80 and 443 in the instance Security List/NSG."
echo "After that, verify:"
echo "  curl -I ${PAIR_URL}/healthz"
echo
echo "Build Blazzing with:"
echo "  -DVIPTV_PAIRING_DEFAULT_URL=${PAIR_URL}"
