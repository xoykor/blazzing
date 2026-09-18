# Blazzing pairing relay

This service replaces LAN pairing. Both the Blazzing client and the phone make
ordinary outbound HTTPS requests to the VPS, so router client isolation, CGNAT
and host inbound-firewall rules on the Blazzing PC are no longer part of the
pairing path.

## Privacy model

The Blazzing client generates a random 128-bit session ID and a random 256-bit
AES key. The key is placed after `#` in the phone URL. URL fragments are not
sent as part of normal HTTP requests.

The browser encrypts this JSON locally with AES-256-GCM:

```json
{"name":"optional name","url":"https://example/playlist.m3u8"}
```

The relay stores only the IV and ciphertext in RAM. Sessions expire after five
minutes and are deleted explicitly after the Blazzing client successfully
decrypts the payload.

The relay serves the JavaScript that performs encryption, so the VPS is still a
trusted delivery point for that JavaScript. The current design prevents routine
server-side logging/storage from seeing playlist credentials; it is not meant
to protect against a maliciously modified relay frontend.

## Oracle VPS layout

The relay listens only on `127.0.0.1:8080`. Caddy terminates public HTTPS on
ports 80/443 and proxies to it.

Build:

```sh
cd relay
go test ./...
CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o blazzing-pairing-relay .
```

Install the binary as `/opt/blazzing-pairing/blazzing-pairing-relay`, create
the unprivileged `blazzing` system user, install
`deploy/blazzing-pairing.service`, and configure Caddy using
`deploy/Caddyfile.example`.

The Oracle Cloud security list / NSG needs inbound TCP 80 and 443. No Blazzing
PC port needs to be opened.

The desktop client will use the public base URL configured by
`VIPTV_PAIRING_URL` until a production relay URL is compiled into a release.
