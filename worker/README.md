# Blazzing pairing Worker

This Cloudflare Worker replaces the self-hosted VPS relay. The public Worker
hosts both the phone page and the pairing API.

Each session is a named SQLite-backed Durable Object. Blazzing generates the
128-bit session ID and 256-bit AES key locally. The browser encrypts the
playlist name/URL with AES-256-GCM. The Worker stores only IV/ciphertext and
expiry state; the AES key is never sent to it.

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
