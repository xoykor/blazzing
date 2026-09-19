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
- M3U parser, categories and shared catalog grid;
- packaged Node.js network service for providers blocked by browser CORS;
- direct browser fetch fallback for Simulator development;
- Xtream authentication;
- Xtream live categories and channels;
- M3U and Xtream parser regression tests.

Still planned:

- Xtream VOD and series;
- catalog pagination/virtualization;
- large-playlist chunking beyond the temporary 8 MiB alpha limit;
- persistence;
- artwork cache;
- store submission and real-TV media validation.

## Prepare

The repository does not commit generated third-party browser files. They are
copied from pinned npm packages into the ignored `app/vendor` directory.

~~~fish
cd webos
./prepare.fish
~~~

The same install also provides the pinned webOS CLI locally under
`webos/node_modules/.bin`.

For webOS TV CLI commands, make sure the CLI profile is `tv` on your machine.

## Run without a TV

With an LG webOS TV Simulator installed:

~~~fish
cd webos
./run-simulator.fish
~~~

The helper defaults to Simulator 25. To select another installed version:

~~~fish
./run-simulator.fish 26
~~~

The Simulator can validate UI, remote navigation, Worker pairing, QR rendering,
M3U parsing, Xtream parsing and most JavaScript behavior. Providers that reject
browser CORS require testing through the packaged JavaScript service rather
than the direct Simulator fallback.

Actual stream/codec compatibility must still be verified on real LG hardware
before a store release.

## Package

~~~fish
cd webos
./package.fish
~~~

The generated IPK is written to `webos/dist` and contains both the app and the
`io.github.xoykor.blazzing.network` JavaScript service.

## Install on a configured TV

After adding the TV as an ares device:

~~~fish
ares-install --device <device-name> dist/*.ipk
ares-launch --device <device-name> io.github.xoykor.blazzing
~~~

Developer Mode credentials remain on the developer machine and are never stored
in this repository.
