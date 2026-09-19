# Blazzing for LG webOS — architecture

Status: alpha implementation  
Branch: `feature/webos-port`

## Runtime

The webOS port is a separate runtime from the Linux C17/X11 application.

It uses:

- HTML/CSS/JavaScript for UI;
- HTML5 `<video>` for playback;
- Web Crypto for AES-256-GCM pairing;
- a packaged JavaScript service for provider networking;
- IndexedDB planned for persistence.

Initial compatibility target: webOS 4.0+.

## Architecture

~~~text
Phone browser
     |
     | encrypted pairing payload
     v
Cloudflare Worker / Durable Object
     ^
     |
     | HTTPS
     |
LG webOS app
     |
     +--> packaged JS network service --> M3U / Xtream API
     |
     +--> direct media URL -----------> LG media pipeline
~~~

The Worker is never used as a media proxy.

## Pairing

Production endpoint:

`https://blazzing-pairing.vsxk.workers.dev`

The TV generates the session ID and AES-256 key. The QR is rendered locally and
contains the AES key only in the URL fragment. The Worker stores ciphertext only.
The TV polls every 2 seconds, decrypts locally and deletes the session.

## M3U

Implemented:

- URL loading;
- CORS-safe packaged service path;
- Simulator browser fallback;
- relative URL resolution;
- EXTINF parsing;
- groups/categories;
- shared catalog UI;
- direct player handoff;
- parser regression tests.

Temporary alpha limits:

- 8 MiB provider response cap;
- 120 DOM items rendered per selected group.

## Xtream live

Implemented:

- credentials kept only in memory;
- authentication through `player_api.php`;
- `get_live_categories`;
- `get_live_streams`;
- category normalization;
- `direct_source` preference;
- fallback generation of `/live/<user>/<pass>/<stream_id>.ts`;
- shared catalog UI;
- regression tests.

The packaged network service has an explicit action allow-list. It is not a
generic open proxy and does not log credentials.

## Compatibility

The app avoids unnecessary modern syntax because older LG TVs use older browser
engines. The packaged service remains ES5-style while webOS 4.x is supported.

## Milestones

### M1 platform
- [x] app metadata
- [x] remote navigation
- [x] Back
- [x] player
- [x] encrypted pairing
- [x] local QR
- [x] Simulator helper
- [ ] real TV smoke test

### M2 M3U
- [x] network service
- [x] bounded fetch
- [x] parser
- [x] categories
- [x] catalog
- [x] playback handoff
- [x] tests
- [ ] virtualization/pagination
- [ ] large-playlist chunking
- [ ] artwork

### M3 Xtream
- [x] authentication
- [x] live categories
- [x] live streams
- [x] tests
- [ ] VOD
- [ ] series/seasons/episodes
- [ ] profile persistence

### M4 parity
- [ ] favorites
- [ ] progress
- [ ] search
- [ ] metadata
- [ ] artwork cache
- [ ] Pluto
- [ ] failover UX

### M5 distribution
- [ ] simulator matrix
- [ ] real LG TV matrix
- [ ] store artwork
- [ ] Seller Lounge metadata
- [ ] QA checklist
