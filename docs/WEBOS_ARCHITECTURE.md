# Blazzing for LG webOS — architecture

Status: alpha implementation  
Branch: `feature/webos-port`

## Goal

Build a native-feeling LG webOS TV client that preserves the Blazzing product
model while using the webOS platform instead of trying to port the Linux/X11
runtime directly.

The Linux application remains the reference implementation for behavior and
provider semantics. The webOS port is a separate frontend/runtime inside the
same repository.

## High-level architecture

~~~text
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
~~~

The Worker is never a video proxy.

## Runtime choices

The Linux app depends on X11/XWayland, Cairo/Pango and mpv. The webOS version
uses:

- HTML/CSS/JavaScript for UI;
- HTML5 `<video>` for playback;
- Web Crypto for AES-256-GCM pairing;
- IndexedDB for future persistence;
- a packaged JavaScript service for provider requests affected by browser CORS.

## Compatibility target

Initial target: webOS 4.0 and newer.

App scripts intentionally use classic scripts/ES5-compatible patterns where
practical. The network service is kept compatible with the old Node.js runtime
used by webOS 4.x: no `const`, `let`, arrow functions or destructuring.

## Repository layout

~~~text
webos/
├── app/
│   ├── appinfo.json
│   ├── index.html
│   ├── css/
│   ├── src/
│   │   ├── app.js
│   │   ├── config.js
│   │   ├── m3u.js
│   │   ├── navigation.js
│   │   ├── network.js
│   │   ├── pairing.js
│   │   ├── player.js
│   │   └── qr.js
│   └── vendor/                 generated, ignored
├── service/
│   └── io.github.xoykor.blazzing.network/
├── tools/
│   └── prepare.mjs
├── prepare.fish
├── run-simulator.fish
├── package.fish
└── package.json
~~~

## Application and service IDs

App:

`io.github.xoykor.blazzing`

Network service:

`io.github.xoykor.blazzing.network`

The service name begins with the application ID as required by webOS packaging.

## Phone pairing

The app reuses:

`https://blazzing-pairing.vsxk.workers.dev`

Flow:

1. TV creates random 128-bit session ID.
2. TV creates random AES-256 key.
3. TV creates the Worker session.
4. TV renders a local QR containing `/pair/<id>#<key>`.
5. Phone encrypts the playlist name/URL using AES-256-GCM.
6. Worker stores only IV/ciphertext plus expiry state.
7. TV polls every 2 seconds.
8. TV decrypts locally.
9. TV deletes the session.
10. The decrypted URL is sent to the M3U loader, not directly treated as a media stream.

QR generation happens locally from a pinned MIT dependency. No external QR
service sees the fragment key.

## M3U flow

~~~text
phone/manual URL
      |
      v
BlazzingNetwork.fetchM3U()
      |
      +-- webOS app + service available --> Luna JS Service --> HTTP/HTTPS
      |
      +-- Simulator/browser fallback ----> fetch() (subject to CORS)
      |
      v
BlazzingM3U.parse()
      |
      v
normalized items + groups
      |
      v
catalog UI --> selected media URL --> HTML5 video
~~~

The current alpha bounds a fetched playlist at 8 MiB. This is deliberately
smaller than the Linux client's large-playlist limit until chunking and memory
behavior are validated on TV hardware.

The catalog currently renders at most 120 items for the selected group. This is
a temporary DOM bound; virtualization/pagination is required before large
catalog support is considered complete.

## Network service security

The service exposes a narrow `fetchM3U` method rather than a generic browser
proxy. It:

- accepts only HTTP/HTTPS URLs;
- follows at most five redirects;
- limits payload size to 8 MiB;
- applies a 15-second request timeout;
- does not log provider URLs or credentials.

Future Xtream and EPG methods should be separate bounded APIs.

## Playback

The Worker is not in the media path. Once a catalog item is selected, its URL is
given directly to the TV's HTML5 media element.

Simulator playback is not sufficient evidence of real TV codec/HLS support, so
media compatibility remains a real-device release gate.

## Milestones

### M1 — platform skeleton

- [x] app metadata
- [x] TV-first UI
- [x] directional remote navigation
- [x] Back handling
- [x] HTML5 player wrapper
- [x] encrypted pairing
- [x] local QR rendering
- [x] Simulator launcher
- [ ] real-TV install smoke test

### M2 — M3U

- [x] packaged network JS service
- [x] bounded M3U fetch
- [x] parser
- [x] category normalization
- [x] catalog grid
- [x] direct HTML5 playback handoff
- [ ] catalog virtualization/pagination
- [ ] large-playlist chunking
- [ ] artwork

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

## Development rules

1. Keep Linux and webOS implementations independent at runtime.
2. Share protocols and data semantics, not X11/mpv implementation details.
3. Keep the Worker backwards-compatible with released Linux clients.
4. Never use the Worker as a media proxy.
5. Prefer remote-control usability over mouse-centric interactions.
6. Keep app code compatible with the declared webOS baseline.
7. Keep service syntax compatible with Node.js 0.12 while webOS 4.x is supported.
8. Every privileged Luna API must document its exact ACG requirement.
9. Bound provider network operations and never create an unrestricted proxy.
