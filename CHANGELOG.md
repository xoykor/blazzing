## 1.4.0 — 2026-09-18

Internet pairing replaces LAN pairing:

- removes the inbound local HTTP server, LAN IP discovery, dynamic/stable pairing ports and PC firewall requirements;
- adds a public HTTPS relay protocol using a random 128-bit session ID and a random 256-bit AES key;
- encrypts playlist name/URL in the phone browser with AES-256-GCM before relay submission;
- keeps the AES key in the QR URL fragment, removes it from the browser address bar/history after bootstrap, and never stores the key on the relay;
- stores only encrypted IV/ciphertext and expiry state in a short-lived SQLite-backed Durable Object, with five-minute expiry and explicit deletion after successful delivery;
- adds verified HTTPS polling/decryption to the C client and rejects non-HTTPS relay base URLs;
- adds a Cloudflare Worker with SQLite-backed Durable Objects, per-IP session-creation rate limiting, response hardening and local smoke tests;
- removes Oracle/Caddy/systemd hosting and deploys the pairing service directly to workers.dev with Wrangler;
- supports a compiled production relay through `VIPTV_PAIRING_DEFAULT_URL` and a development override through `VIPTV_PAIRING_URL`;
- updates current EN/PT-BR documentation to the Internet relay architecture.

## 1.3.3 — 2026-09-17

Phone-pairing LAN reachability fix:

- prefers physical Wi-Fi/Ethernet IPv4 addresses over Docker, Podman, VPN, WireGuard, Tailscale and other virtual interfaces when building the QR URL;
- uses TCP port `47831` as the preferred pairing port so firewall configuration remains stable across sessions;
- falls back to an automatically selected free port if `47831` is already occupied;
- shows the actual TCP port in the pairing UI when a firewall exception may be required;
- documents UFW/CachyOS troubleshooting and guest-Wi-Fi/client-isolation limitations;
- adds regression coverage that starts two pairing servers simultaneously and verifies the second one falls back to a different port.

## 1.3.2 — 2026-09-17

Remote-control navigation and phone-assisted playlist entry:

- adds directional focus navigation across the login screen, saved profiles, top catalog controls, search, category sidebar and media grid;
- accepts X11 Return/KP Enter/Select as OK and common Back/Escape/Backspace mappings as context-aware return actions;
- adds a temporary local HTTP pairing server with automatic free-port allocation, routed LAN address discovery and a random one-time token;
- displays a locally generated QR Code so M3U/M3U8 URLs can be pasted from a phone on the same trusted LAN;
- falls back to localhost-only pairing when no usable LAN address is available instead of displaying an unreachable QR Code;
- hardens the pairing page with no-store/no-referrer/CSP/nosniff/frame-deny headers and validates HTTP/HTTPS playlist URLs;
- adds pairing-server regression tests for successful submission, invalid tokens, invalid schemes and browser-security headers;
- adds `VIPTV_INPUT_DEBUG=1` to identify unusual TV-remote key mappings;
- adds a discreet `by Xoykor` credit to the initial screen;
- bundles libqrencode in the Flatpak and documents the new dependency and remote-control workflow.

## 1.3.1 — 2026-09-17

Maintenance and maintainability release:

- publishes the finalized post-v1.3.0 maintenance pass as an official Flatpak release;
- reformats and restructures the C source for readability without changing the frozen feature scope;
- adds complete function/prototype comment coverage and refreshes project documentation;
- removes stale mpv/X11 reparenting code and its unnecessary player-library X11 dependency;
- validates the maintained codebase with static analysis, sanitizers and stress testing.

## 1.3.0 — 2026-09-17

Visual rendering and interaction overhaul:

- introduces a shared Cairo/Pango rendering layer for antialiased rounded surfaces, gradients and proportional UTF-8 typography while retaining the native C17/X11 architecture;
- makes IPTV catalog cards responsive to available width and modernizes search caret metrics, top tabs, sidebar navigation, loading states and card metadata;
- adds reusable UI motion primitives for hover/focus transitions and smooth scrolling, with dedicated regression coverage;
- redesigns the startup hub with correct UTF-8 rendering, modern service cards and pointer hover feedback;
- finishes the IPTV login, saved-profile panel, metadata/details surface and embedded-player HUD using the same visual system;
- modernizes Pluto TV header, channel grid, loading/status states and player controls, and prevents cards from being obscured by the status footer;
- preserves existing keyboard/mouse navigation, providers, thumbnail scheduling and playback behavior while replacing legacy bitmap-font presentation paths;
- validates the redesign through Release tests, ASan/UBSan, Flatpak builds and automated Xvfb screenshot QA at 1600×900.

## 1.2.16 — 2026-09-17

M3U catalog routing and series-navigation fix:

- M3U playlists are now split into live TV, movies and series instead of putting every item in the TV tab;
- movie and series group-title categories populate the correct tabs for M3U sources;
- M3U episode entries are collapsed into one series card in the series root view;
- selecting an M3U series now opens season selection first and then the episodes for the chosen season;
- the search field now has a much stronger active-focus state with highlighted background, accent rail and caret;
- player fullscreen now tracks the window manager state, retries EWMH fullscreen under KDE/XWayland and falls back to a borderless screen-sized window when needed;
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
