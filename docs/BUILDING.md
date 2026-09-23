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

The Windows port is built separately from the native Linux CMake target.

Requirements:

- Windows 10 or newer, x64;
- Node.js 20 or newer for source builds;
- `mpv.exe` available on `PATH`, through `VIPTV_MPV_PATH`, or in one of the locations documented by `windows/README.md`.

Run from source:

```powershell
cd windows
npm install
npm start
```

Build installer + portable artifacts:

```powershell
npm run dist
```

The generated frontend under `windows/app/` is copied from `tizen/` by `npm run prepare-app`; it must not be edited directly. Windows packaging is also validated by `.github/workflows/windows.yml`.
