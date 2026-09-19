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
- localStorage for small non-secret state such as favorites; IndexedDB remains reserved for larger caches.

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

Large-playlist behavior:

- packaged webOS service accepts M3U/M3U8 playlists up to **128 MiB**;
- playlists are streamed to a private temporary file instead of being held as one giant JavaScript string;
- categories and total item count are extracted while the download is being written;
- catalog pages are queried from disk on demand;
- only 48 media cards are rendered at a time;
- temporary playlist sessions expire after 12 hours and are also released when leaving the catalog;
- if a paged M3U session disappears while the catalog is open, the app re-downloads and reindexes the source automatically;
- the source URL used for that recovery stays in memory only and is never persisted by the favorites/progress store;
- Xtream/API JSON responses keep the separate 8 MiB safety cap.

## Xtream

Implemented:

- passwords kept only in memory;
- optional local profile persistence stores only server URL + username after a successful login;
- authentication through `player_api.php`;
- `get_live_categories`;
- `get_live_streams`;
- VOD categories and streams;
- VOD detail metadata through `get_vod_info`;
- series detail metadata from `get_series_info`;
- series, seasons and episodes;
- category normalization;
- `direct_source` preference;
- fallback generation of `/live/<user>/<pass>/<stream_id>.ts`;
- shared catalog UI;
- regression tests.

The packaged network service has an explicit action allow-list. It is not a
generic open proxy and does not log credentials.

## Favorites

Favorites are stored locally using opaque stable keys plus safe display metadata.
The persistence layer deliberately does not store media URLs, Xtream usernames or
Xtream passwords. M3U URLs are fingerprinted before an item key is persisted.

The catalog always exposes a **Favoritos** group. The yellow remote key toggles the
focused item; the Simulator also accepts **F**.

## Artwork

Catalog items render provider artwork when an HTTP/HTTPS image is available and
fall back to a lightweight initial tile when it is not. Images use lazy-loading
hints and no-referrer requests. The page window is intentionally limited to 48
items to reduce decoded-image pressure on older TVs. A persistent artwork cache
is still planned.

## Search

The catalog can filter the currently loaded source by title or category. For
large M3U sources the query is executed against the temporary disk-backed
playlist and returns only one page. The search value remains part of catalog
navigation state, so opening an Xtream series and pressing Back restores the
previous query, category and page.

## Playback and failover

Movies and Xtream episodes persist only a playback timestamp keyed by the same
opaque item identifier used by favorites. Progress is checkpointed approximately
every 15 seconds and again when leaving the player. Items within 30 seconds of the
end are treated as completed and their checkpoint is removed.

For Xtream Live/VOD entries that provide a `direct_source`, the catalog also keeps
the provider-generated stream URL in memory as a fallback. If the direct source
fails, the player automatically tries the generated Xtream route. If all known
routes fail, the player exposes a remote-friendly **Tentar novamente** action and
retries from the saved playback position when applicable.

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
- [x] disk-backed pagination window (48 items)
- [x] M3U/M3U8 up to 128 MiB without loading the full playlist into UI memory
- [ ] full virtualization
- [x] artwork rendering

### M3 Xtream
- [x] authentication
- [x] live categories
- [x] live streams
- [x] tests
- [x] VOD
- [x] series/seasons/episodes
- [x] profile persistence without password storage

### M4 parity
- [x] favorites
- [x] progress
- [x] search
- [x] VOD metadata
- [x] series metadata
- [ ] live metadata
- [ ] artwork cache
- [ ] Pluto
- [x] failover UX

### M5 distribution
- [ ] simulator matrix
- [ ] real LG TV matrix
- [ ] store artwork
- [ ] Seller Lounge metadata
- [ ] QA checklist
