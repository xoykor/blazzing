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
- localStorage for small non-secret state such as favorites;
- IndexedDB for the bounded artwork cache.

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
     +--> packaged JS network service --> M3U / Xtream / Pluto APIs
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
- Live channel number, EPG channel ID and catch-up duration metadata;
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
hints and no-referrer requests.

Artwork is cached in IndexedDB using only an opaque hash of the source URL as the
persistent key; raw provider image URLs are not stored in the cache index. The
network service accepts at most 1 MiB per artwork response. The persistent cache
is capped at 24 MiB and 256 items, expires entries after 7 days and prunes least
recently used entries. Object URLs are revoked when cards/details leave the DOM.

The page window remains limited to 48 items to reduce decoded-image pressure on
older TVs. Remote navigation now crosses page boundaries automatically when the
focus reaches the end of the current card window, so the user does not have to
move to the pagination buttons during normal browsing.

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

## Pluto TV

Pluto TV is integrated directly through the packaged webOS network service. The
service creates a regional session using the current `boot.pluto.tv/v4/start`
flow and keeps the session JWT only in service memory.

Live channels prefer the current guide-v2 channel/category APIs and fall back to
the legacy channel endpoint when needed. The UI catalog stores only channel IDs
and safe display metadata; the signed HLS URL is created only when playback starts.

VOD uses the current `/v3/vod/categories?includeItems=true&deviceType=web`
catalog with a category-page size of 1000. Duplicate films/series appearing in
multiple categories are collapsed by Pluto content ID. Series details are loaded
from `/v3/vod/series/<id>/seasons?includeItems=true&deviceType=web`.

Movie and episode catalogue entries retain only a sanitized stitcher path such as
`/stitch/hls/...`. Old host names, query strings and JWTs returned in catalogue
metadata are discarded. At playback time the service converts that path to the
current v2 stitcher URL and attaches the current in-memory session JWT.

Favorites and playback progress use opaque Pluto content IDs; no Pluto JWT or
signed media URL is persisted.

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
- [x] seamless bounded-window navigation (48 cards kept in DOM)
- [ ] true scroll virtualization within a single logical page
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
- [x] live metadata
- [x] artwork cache
- [x] Pluto Live/VOD
- [x] failover UX

### M5 distribution
- [ ] simulator matrix
- [ ] real LG TV matrix
- [ ] store artwork
- [ ] Seller Lounge metadata
- [ ] QA checklist
- [x] CI release metadata validation
- [x] CI-generated installable IPK artifact
