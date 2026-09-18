# Blazzing for LG webOS — architecture

Status: initial implementation  
Branch: `feature/webos-port`

## Goal

Build a native-feeling LG webOS TV client that preserves the Blazzing product
model while using the webOS platform instead of trying to port the Linux/X11
runtime directly.

The Linux application remains the reference implementation for behavior and
provider semantics. The webOS port is a separate frontend/runtime inside the
same repository.

## High-level architecture

```text
                         +----------------------+
                         | Cloudflare Worker    |
                         | pairing only         |
                         | Durable Objects      |
                         +----------+-----------+
                                    ^
                                    | HTTPS, encrypted payload only
                                    |
     phone browser -----------------+---------------- LG webOS app
                                                        |
                                                        | direct media URL
                                                        v
                                                IPTV/media server
```

The Worker is never a video proxy. It is used only for the small encrypted
phone-pairing payload.

## Why this is not a C/X11 port

The desktop application depends on X11/XWayland, Cairo/Pango and mpv. webOS TV
applications are web applications packaged as IPK files and use the TV browser
engine plus platform services.

The webOS implementation therefore uses:

- HTML/CSS/JavaScript for UI;
- the native HTML5 `<video>` pipeline for playback;
- Web Crypto for AES-256-GCM pairing;
- IndexedDB for application data;
- a packaged JavaScript service for provider/network requests that cannot be
  performed safely/reliably from the browser context because of CORS.

## Compatibility target

Initial target: webOS 4.0 and newer.

Code in `webos/app` deliberately avoids modern syntax that would unnecessarily
raise the minimum browser-engine requirement. The first implementation uses
classic scripts rather than JavaScript modules.

Future compatibility work must be tested on real LG hardware or the official
simulator before lowering or raising this baseline.

## Repository layout

```text
blazzing/
├── src/                         Linux C17 implementation
├── worker/                      shared Cloudflare pairing service
├── docs/
│   └── WEBOS_ARCHITECTURE.md
└── webos/
    ├── README.md
    ├── package.fish
    └── app/
        ├── appinfo.json
        ├── icon.png
        ├── index.html
        ├── css/
        │   └── app.css
        └── src/
            ├── config.js
            ├── navigation.js
            ├── pairing.js
            ├── player.js
            └── app.js
```

A future network service will live under:

```text
webos/service/io.github.xoykor.blazzing.network/
```

The service name must begin with the application ID so it can be packaged and
registered correctly by webOS.

## Application ID

```text
io.github.xoykor.blazzing
```

The ID intentionally contains no hyphen and no numeric dotted component so it
remains compatible with a future Luna JavaScript service name.

## appinfo.json

The webOS package metadata lives at `webos/app/appinfo.json`.

The initial app declares:

- type: `web`;
- main: `index.html`;
- 80x80 PNG app icon;
- `requiredACG: []`.

The explicit empty `requiredACG` is intentional. The first milestone does not
call privileged Luna APIs. If later code uses a Luna API, the exact required ACG
groups must be added rather than granting broad permissions.

## UI model

The UI is designed for 10-foot viewing and remote-control navigation.

Primary screens:

1. Home
2. Pairing
3. Manual stream URL
4. Player
5. Catalog — planned
6. Details — planned
7. Settings — planned

Input sources:

- standard directional remote;
- OK/Enter;
- Back;
- Magic Remote pointer, treated as ordinary pointer interaction.

The focus layer is independent from page business logic. Focusable elements use
the `.focusable` class and `navigation.js` chooses the nearest target in the
requested direction using element geometry.

## Phone pairing

The webOS app reuses the production Worker:

```text
https://blazzing-pairing.vsxk.workers.dev
```

Protocol is the same as Blazzing Linux:

1. TV generates a random 128-bit session ID.
2. TV generates a random 256-bit AES key.
3. TV creates the session with the Worker.
4. TV constructs:
   `/pair/<session-id>#<base64url-key>`.
5. Phone opens the pairing page.
6. Phone encrypts playlist name/URL with AES-256-GCM.
7. Worker stores only IV/ciphertext plus expiration state.
8. TV polls every 2 seconds.
9. TV decrypts locally with Web Crypto.
10. TV deletes the session.

The AES key is not sent in normal HTTP requests because it is carried in the
URL fragment.

The current first milestone exposes the pairing URL on screen. QR rendering is
the next UI task; the crypto/session implementation is already separated so the
QR renderer can be added without changing the protocol.

## Playback

The webOS port uses an HTML5 `<video>` element.

```text
provider/media server ---> LG media pipeline
```

The Cloudflare Worker is not in the media path.

The player module currently provides:

- open URL;
- play;
- pause/resume;
- stop;
- native media error reporting.

Later work must add:

- live/VOD state handling;
- seek controls for seekable VOD;
- audio/subtitle track selection where supported;
- stream failover;
- playback progress persistence;
- codec/capability diagnostics.

## M3U and Xtream networking

Browser-side `fetch()` is subject to CORS. Many IPTV servers do not return
browser-friendly CORS headers.

Therefore the final provider architecture is:

```text
web app
   |
   | Luna request
   v
Blazzing packaged JS network service
   |
   | Node HTTP/HTTPS
   v
provider API / M3U / EPG
```

The service must expose narrow methods such as:

- `fetchM3U`;
- `fetchXtream`;
- `fetchEPG`.

It must not become an unrestricted URL proxy.

The video stream itself should still be handed directly to the native media
pipeline whenever possible.

## Provider layer

Planned provider modules mirror desktop behavior rather than desktop code:

```text
Provider
├── M3U
├── Xtream
└── Pluto
```

Normalized media model:

```text
MediaItem
- id
- provider
- type: live | movie | series | episode
- title
- group/category
- artwork
- streamUrl
- metadata
```

The UI consumes this normalized model instead of provider-specific payloads.

## Persistence

Use IndexedDB for:

- saved profiles;
- M3U provider metadata;
- catalog cache where useful;
- favorites;
- VOD/episode progress;
- settings.

Passwords and other sensitive account material need a separate review before
being persisted on webOS. Do not automatically copy the Linux Secret Service
design because that API does not exist unchanged on TV.

## Security boundaries

- Pairing requires HTTPS.
- Pairing key remains local to TV and phone.
- Worker receives encrypted payload only.
- Worker never proxies IPTV streams.
- Provider credentials must never be written to console logs.
- Playlist/stream URLs must be redacted from retained diagnostics.
- Network service methods must validate URL schemes and bound response sizes.
- No generic open proxy method.
- No wildcard privileged ACG grants.
- Session expiration remains five minutes.

## Initial milestone implemented in this branch

The first code drop includes:

- packageable webOS application skeleton;
- app metadata and requiredACG declaration;
- TV-oriented responsive home UI;
- directional focus navigation;
- Back handling;
- direct URL playback using HTML5 video;
- production Cloudflare pairing client;
- AES-256-GCM decryption on the TV;
- 2-second pairing polling;
- session cleanup;
- basic CI syntax/config validation;
- fish-compatible packaging helper.

This is intentionally a vertical slice before provider/catalog work.

## Milestones

### M1 — platform skeleton

- [x] appinfo.json
- [x] base UI
- [x] remote navigation
- [x] HTML5 player wrapper
- [x] pairing protocol client
- [ ] QR rendering
- [ ] real-TV install smoke test

### M2 — M3U

- [ ] packaged network JS service
- [ ] bounded M3U fetch
- [ ] parser
- [ ] category normalization
- [ ] catalog grid
- [ ] direct HLS playback

### M3 — Xtream

- [ ] authentication
- [ ] live categories
- [ ] VOD
- [ ] series/seasons/episodes
- [ ] profile storage

### M4 — product parity

- [ ] favorites
- [ ] progress
- [ ] search
- [ ] metadata
- [ ] artwork cache
- [ ] Pluto
- [ ] error/failover UX

### M5 — distribution

- [ ] simulator matrix
- [ ] real LG TV matrix
- [ ] icon/store artwork
- [ ] Seller Lounge metadata
- [ ] QA checklist
- [ ] signed/public release process

## Packaging

With the current webOS CLI installed:

```fish
cd webos
./package.fish
```

The helper runs `ares-package` and writes the IPK into `webos/dist`.

When the JavaScript network service is added, the packaging helper must package
both the app directory and service directory.

## Development rules

1. Keep Linux and webOS implementations independent at runtime.
2. Share protocols and data semantics, not X11/mpv implementation details.
3. Keep the Worker backwards-compatible with released Linux clients.
4. Avoid adding a cloud proxy for media traffic.
5. Prefer remote-control usability over mouse-centric interactions.
6. Keep code compatible with the declared webOS baseline.
7. Every new Luna API must document and declare its ACG requirement.
8. Test packaging and navigation before adding large provider features.
