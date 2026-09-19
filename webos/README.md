# Blazzing for LG webOS

Early webOS port of Blazzing.

Architecture: [../docs/WEBOS_ARCHITECTURE.md](../docs/WEBOS_ARCHITECTURE.md)

## Current scope

Implemented:

- packageable web app + JavaScript service;
- remote directional navigation, OK, Back and pointer focus;
- HTML5 video wrapper;
- encrypted phone pairing using the production Blazzing Worker;
- local QR rendering;
- M3U/M3U8 URL loading;
- M3U parser, categories and catalog grid;
- provider artwork with lightweight fallback tiles;
- packaged Node.js network service for providers blocked by browser CORS;
- direct browser fallback for Simulator development;
- Xtream authentication;
- Xtream live categories and channels;
- Xtream VOD;
- Xtream series, seasons and episodes;
- catalog search by title/category;
- persistent favorites without storing credentials or media URLs;
- playback progress/resume for Xtream movies and episodes;
- regression tests for M3U, Xtream and persistence.

Still planned:

- full catalog virtualization beyond the current 48-item page window;
- large-playlist handling beyond the temporary 8 MiB alpha limit;
- artwork cache;
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
Actual codec/HLS compatibility still requires real LG hardware.

## Package

~~~fish
cd webos
./package.fish
~~~

The generated IPK in `webos/dist` contains both the app and
`io.github.xoykor.blazzing.network`.

## Install on a configured TV

~~~fish
ares-install --device <device-name> dist/*.ipk
ares-launch --device <device-name> io.github.xoykor.blazzing
~~~
