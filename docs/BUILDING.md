# Building and installation

[Português (Brasil)](BUILDING.pt-BR.md)

The published v1.4.7 Flatpak is the recommended end-user installation. Source builds are intended for development, auditing and maintenance.

## CachyOS / Arch Linux

```sh
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl cairo pango ffmpeg mpv libsecret
```

Build and test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Fish helper:

```fish
./scripts/build-cachyos.fish
```

## Debian / Ubuntu

```sh
sudo apt update
sudo apt install git build-essential cmake pkg-config libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev libjpeg-dev libpng-dev libwebp-dev libssl-dev libcairo2-dev libpango1.0-dev ffmpeg mpv libsecret-tools
```

Then use the same CMake commands.

## Sanitizers

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-san --parallel
ctest --test-dir build-san --output-on-failure
```

`VIPTV_SANITIZE` enables AddressSanitizer and UndefinedBehaviorSanitizer with GCC or Clang.

## Local installation

```sh
sudo cmake --install build --prefix /usr/local
```

The historical executable name is `visual-iptv`. The Flatpak application ID is `io.github.xoykor.Blazzing`.

## Runtime

Required:

- Linux with X11 or XWayland;
- mpv;
- FFmpeg;
- the libraries discovered by CMake.

`secret-tool` is optional. When available it provides Secret Service password storage.

The player passes its X11 video child window to mpv with `--wid`; there is no native Wayland playback path in this release.

## Compile database

`CMAKE_EXPORT_COMPILE_COMMANDS` is enabled. Editors and analysis tools can use `build/compile_commands.json`.


## Windows desktop

Windows builds the same native C17 target as Linux using MSYS2 MinGW64 and `-DVIPTV_USE_SDL2=ON`.

Requirements:

- Windows 10 or newer, x64;
- MSYS2 (`MINGW64` environment) with `gcc`, `cmake`, `ninja`, `pkgconf`, `SDL2`, `cairo`, `pango`, `curl`, `json-c`, `sqlite3`, `openssl`, `libjpeg-turbo`, `libpng`, `libwebp`, and `qrencode`;
- `mpv.exe` alongside `visual-iptv.exe`, inside `mpv/mpv.exe`, on `PATH`, or via `VIPTV_MPV_PATH`.

Build and run from source (MSYS2 MinGW64):

```sh
cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DVIPTV_USE_SDL2=ON
cmake --build build-win
ctest --test-dir build-win --output-on-failure
./build-win/visual-iptv.exe
```

Windows compilation, unit tests, and portable packaging are validated by `.github/workflows/windows.yml`.
