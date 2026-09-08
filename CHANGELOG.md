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
