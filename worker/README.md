# Blazzing pairing Worker

This Cloudflare Worker replaces the self-hosted VPS relay. The public Worker
hosts both the phone page and the pairing API.

Each session is a named SQLite-backed Durable Object. Blazzing generates the
128-bit session ID and 256-bit AES key locally. The browser encrypts the
playlist name/URL with AES-256-GCM. The Worker stores only IV/ciphertext and
expiry state; the AES key is never sent to it.

## Session model

- session TTL: **45 seconds**;
- clients poll at a 5-second cadence;
- each session uses a random 128-bit identifier;
- the AES-256 key is generated client-side and is never sent to the Worker;
- Durable Objects store only encrypted payload/IV plus expiry state;
- expired sessions return HTTP 410 and clients can create a fresh session.

The same relay protocol is used by the Linux desktop client and the Samsung Tizen port.

## Local test

```sh
cd worker
npm install
npm run check
npm run smoke
```

## Deploy

```sh
cd worker
npm install
npx wrangler login
npm run deploy
```

Wrangler prints the public `https://...workers.dev` URL. Build Blazzing with:

```sh
cmake -S . -B build \
  -DVIPTV_PAIRING_DEFAULT_URL=https://blazzing-pairing.<subdomain>.workers.dev
```

`VIPTV_PAIRING_URL` overrides the compiled URL for development.

No Oracle VM, Caddy, systemd, public inbound port, custom domain, or GitHub
Pages site is required.


## Security boundary

The Worker is a short-lived relay, not a playlist proxy. It does not fetch or play the submitted M3U/M3U8 URL and does not need the plaintext playlist data. Confidentiality depends on clients keeping the fragment-carried AES key local and using the expected HTTPS relay endpoint.

## License

GNU General Public License v3.0. See [../LICENSE](../LICENSE).
