# Blazzing pairing relay

This service replaces the old LAN pairing path completely. Both Blazzing and
the phone make normal outbound HTTPS requests to the Oracle VPS. The Blazzing
PC no longer opens a listening port, so client isolation, CGNAT and the local
firewall do not affect phone pairing.

## Protocol

1. Blazzing generates a random 128-bit session ID and a random 256-bit AES key.
2. Blazzing creates a five-minute session with the relay over HTTPS.
3. The QR opens `/pair/<session>#<key>`.
4. The browser reads the key from the URL fragment, immediately removes the
   fragment from the address bar/history, and encrypts the playlist data with
   AES-256-GCM.
5. The relay stores only the IV and ciphertext in RAM.
6. Blazzing polls the session over HTTPS, receives the encrypted payload and
   decrypts it locally.
7. On success the remote session is deleted. Expired sessions are also removed
   automatically.

The payload before encryption is only:

```json
{"name":"optional name","url":"https://example/playlist.m3u8"}
```

The key after `#` is not part of ordinary HTTP requests. The relay frontend
itself is still a trusted delivery point because it serves the JavaScript that
performs encryption; this design prevents normal relay storage/logging from
seeing playlist credentials, but it is not intended to defend against a
maliciously modified relay frontend.

## Oracle Cloud deployment

Recommended image: Ubuntu 24.04 LTS. The same source builds natively on Oracle
AMD64 and Ampere ARM64 instances.

The quickest install from a Blazzing checkout is:

```sh
sudo bash relay/deploy/install-oracle-ubuntu.sh
```

The installer:

- tests and compiles the Go relay;
- installs the official Caddy package;
- runs the relay as an unprivileged `blazzing` systemd service on
  `127.0.0.1:8080`;
- detects the public IPv4 and creates a domain-free hostname such as
  `203-0-113-10.sslip.io`;
- configures Caddy for public HTTPS;
- opens 80/443 in UFW if UFW is active;
- prints the final relay URL and the CMake option to embed it in Blazzing.

Oracle Cloud still needs inbound TCP **80 and 443** allowed in the instance
Security List or NSG. No inbound port is required on a user's Blazzing PC.

If you already own a domain, edit `/etc/caddy/Caddyfile` to use it instead of
the generated sslip.io hostname and restart Caddy.

## Manual build

```sh
cd relay
go test ./...
CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o blazzing-pairing-relay .
```

The service listens on `127.0.0.1:8080` by default. Override it with
`BLZ_RELAY_ADDR` only when needed.

## Blazzing client configuration

For development:

```sh
VIPTV_PAIRING_URL=https://pair.example.com ./build/visual-iptv
```

For a production build:

```sh
cmake -S . -B build \
  -DVIPTV_PAIRING_DEFAULT_URL=https://pair.example.com
```

The environment variable overrides the compiled default and is mainly useful
for testing.
