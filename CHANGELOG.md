## 1.2.16 — 2026-09-17

M3U catalog routing and series-navigation fix:

- M3U playlists are now split into live TV, movies and series instead of putting every item in the TV tab;
- movie and series group-title categories populate the correct tabs for M3U sources;
- M3U episode entries are collapsed into one series card in the series root view;
- selecting an M3U series now opens season selection first and then the episodes for the chosen season;
- the search field now has a much stronger active-focus state with highlighted background, accent rail and caret;
- browser-launched Prime Video, Max and Globoplay entries and their external-session API were removed completely;
- the home hub now contains only native IPTV/list playback and Pluto TV.

## 1.2.15 — 2026-09-17

UI/UX and series-navigation update:

- redesigns the main IPTV interface with a darker blue visual system, rounded surfaces and clearer typography hierarchy;
- refreshes login, catalog browsing, metadata/details and the embedded-player HUD while preserving existing keyboard and mouse navigation;
- series now open with a season-first flow, then show the episodes for the selected season;
- redesigns the streaming hub and labels DRM services as external browser sessions instead of imported/integrated account sessions;
- keeps the existing Secret Service identifier for backward compatibility while presenting new saved credentials under the Blazzing label;
- validates the combined redesign with the full 11-test suite.

## 1.2.14 — 2026-09-17

Reliability, security and thumbnail performance hardening:

- thumbnail shutdown now stops promptly instead of draining the full pending queue;
- provider/profile switches discard stale pending thumbnail work while ordinary scrolling and filtering keep cache warming intact;
- corrupt JPEG artwork is handled as a normal decode failure instead of allowing libjpeg to terminate the process;
- artwork downloads abort stalled low-speed transfers sooner, freeing workers for useful work;
- background prefetch spends most of its budget on the active catalog before warming hidden catalogs;
- mpv JSON IPC buffer reset and compaction paths are hardened;
- Secret Service password writes now handle short writes and interruptions correctly;
- external DRM services are clearly presented as browser-launched sessions rather than integrated account login;
- Release, ASan/UBSan, Cppcheck and GCC analyzer validation cover the final hardening changes.

## 1.2.13 — 2026-09-17

Large M3U playlist support:

- raises the local and remote M3U/M3U8 playlist limit from 32 MiB to 128 MiB;
- large playlists such as ~79 MiB catalogs can now be loaded directly;
- HTTP redirects remain supported for shortened/direct playlist URLs;
- size-limit error messages now stay in sync with the configured limit.

## 1.2.12 — 2026-09-08

Automatic provider endpoint discovery:

- when a normal Xtream login returns HTTP 404, Blazzing can query the StreamFire/Spark resolver APIs;
- resolver payload decoding and candidate extraction are implemented natively in C;
- returned bases are normalized, deduplicated and verified through `player_api.php`;
- the first verified base becomes the primary server and a second verified base becomes failover;
- credentials are not sent to resolver APIs during normal successful Xtream logins;
- regression tests cover payload decoding and server extraction.

# Changelog

[Português (Brasil)](CHANGELOG.pt-BR.md)

## 1.2.11 — 2026-09-07

Thumbnail stall prevention update:

- background prefetch no longer opens streams for items without provider artwork;
- logo-less stream-frame thumbnails are generated only for visible/interactive cards;
- FFmpeg thumbnail capture is capped at two concurrent jobs so artwork downloads keep progressing;
- scrolling/filter rebuilds no longer destroy the pending thumbnail queue;
- regression coverage verifies background no-artwork filtering and queue preservation during viewport changes;
- continuous aggressive artwork prefetch remains enabled with the existing bounded scheduler queue.

## 1.2.10 — 2026-09-07

Thumbnail pipeline reliability update:

- bounded background queue to prevent runaway prefetch backlog;
- fair prefetch priority across live TV, VOD and series;
- continuous aggressive prefetch without flooding the provider;
- failed artwork downloads no longer spawn expensive FFmpeg fallbacks;
- atomic `.tmp` + rename cache writes;
- corrupt cached JPEGs are discarded and fetched again;
- thumbnail failures now emit sampled diagnostics to stderr.

## 1.2.7 — 2026-09-07

Initial public baseline, originally released under the working name Visual IPTV and now published as **Blazzing**:

- Xtream Codes and M3U support;
- live TV, VOD, series, and episodes;
- visual navigation and adaptive artwork;
- asynchronous thumbnails;
- favorites, profiles, metadata, and playback progress in SQLite;
- persistent mpv controlled through JSON IPC with embedded native X11 window;
- HUD, timeline, pause/seek/volume/fullscreen, and failover;
- test suite for the main modules.
