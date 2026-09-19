# Blazzing for LG webOS

Early webOS port of Blazzing.

Architecture: [../docs/WEBOS_ARCHITECTURE.md](../docs/WEBOS_ARCHITECTURE.md)

Release QA: [QA_CHECKLIST.md](QA_CHECKLIST.md)  
Seller Lounge draft: [SELLER_LOUNGE.md](SELLER_LOUNGE.md)

## Current scope

Implemented:

- packageable web app + JavaScript service;
- remote directional navigation, OK, Back and pointer focus;
- HTML5 video wrapper;
- encrypted phone pairing using the production Blazzing Worker;
- local QR rendering;
- M3U/M3U8 URL loading up to 128 MiB on the packaged webOS service;
- disk-backed M3U catalog paging so large playlists do not live entirely in UI RAM;
- seamless remote traversal between 48-card catalog windows;
- automatic recovery if a temporary paged M3U session expires or the service restarts;
- M3U parser, categories and catalog grid;
- provider artwork with lightweight fallback tiles and bounded IndexedDB cache;
- packaged Node.js network service for providers blocked by browser CORS;
- direct browser fallback for Simulator development;
- Xtream authentication;
- optional remembered Xtream server + username, never the password;
- Xtream live categories/channels with channel number and catch-up metadata;
- Xtream VOD plus movie detail metadata;
- Xtream series detail metadata, seasons and episodes;
- direct regional Pluto TV Live/VOD catalogues, series/episodes and HLS session handling;
- catalog search by title/category;
- persistent favorites without storing credentials or media URLs;
- playback progress/resume for Xtream movies and episodes;
- automatic Xtream direct-source failover plus manual retry in the TV player;
- regression tests for M3U, Xtream, persistence and the UI/DOM contract.

Still planned:

- true scroll virtualization beyond the current seamless 48-item card window;
- store submission and real-TV media validation.

## Prepare

~~~fish
cd webos
./prepare.fish
~~~

## Run without a TV

~~~fish
cd webos
./run-simulator.fish
~~~

To choose another installed Simulator version:

~~~fish
./run-simulator.fish 26
~~~

The Simulator validates UI, navigation, pairing, QR, parsers and most JavaScript behavior.
The 128 MiB low-memory path is specifically implemented by the packaged webOS
network service; direct browser fallback may use more RAM. Actual codec/HLS
compatibility still requires real LG hardware.

## Package

~~~fish
cd webos
./package.fish
~~~

The generated IPK in `webos/dist` contains both the app and
`io.github.xoykor.blazzing.network`.

Pull-request CI also builds the installable IPK and exposes it as the
`blazzing-webos-ipk` workflow artifact. App, service and development package
versions are kept aligned and validated before packaging.

## Install on a configured TV

~~~fish
ares-install --device <device-name> dist/*.ipk
ares-launch --device <device-name> io.github.xoykor.blazzing
~~~
