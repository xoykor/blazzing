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
- packaged Node.js network service for providers blocked by browser CORS;
- direct browser fallback for Simulator development;
- Xtream authentication;
- Xtream live categories and channels;
- regression tests for M3U and Xtream.

Still planned:

- Xtream VOD and series;
- catalog pagination/virtualization;
- large-playlist handling beyond the temporary 8 MiB alpha limit;
- persistence and favorites;
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
