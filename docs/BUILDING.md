# Building and installation

[Português (Brasil)](BUILDING.pt-BR.md)

## CachyOS / Arch Linux

Dependencies:

```sh
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl ffmpeg mpv libsecret
```

Release build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Tests:

```sh
ctest --test-dir build --output-on-failure
```

Equivalent helper script:

```fish
./scripts/build-cachyos.fish
```

## Debian / Ubuntu

A typical dependency set is:

```sh
sudo apt update
sudo apt install git build-essential cmake pkg-config libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev libjpeg-dev libpng-dev libwebp-dev libssl-dev ffmpeg mpv libsecret-tools
```

Then use the same CMake commands.

## Sanitizers

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

`VIPTV_SANITIZE` enables AddressSanitizer and UndefinedBehaviorSanitizer with GCC/Clang.

## Local installation

After building:

```sh
sudo cmake --install build --prefix /usr/local
```

CMake currently installs:

- `visual-iptv` into `bin`;
- `packaging/visual-iptv.desktop` into `share/applications`.

For development, prefer running `./build/visual-iptv` directly.

## `compile_commands.json`

The project enables `CMAKE_EXPORT_COMPILE_COMMANDS=ON`. The file is generated inside the build directory. Editors and tools such as clangd can point to `build/compile_commands.json`.

## Runtime requirements

The build may locate mpv, but mpv is also a runtime dependency. The current player integration expects a working X11 session and an mpv build capable of creating a native X11 window.
