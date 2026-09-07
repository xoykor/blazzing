# Blazzing

<p align="center">\n  <img src="assets/blazzing.png" alt="Blazzing icon" width="220">\n</p>\n\n**English** | [Português (Brasil)](README.pt-BR.md)

Blazzing is a native IPTV desktop client for Linux/X11, written in C17 and designed around one main goal: **a fast, responsive visual interface**.

It supports Xtream Codes and M3U playlists and embeds mpv for playback.

> Use Blazzing only with playlists, servers, and content you are authorized to access.

## Current features

- Separate catalogs for live TV, movies, and series.
- Xtream Codes support with primary and alternate server failover.
- Remote and local M3U/M3U8 playlists.
- Adaptive visual grid for portrait, landscape, and square artwork.
- Asynchronous thumbnail downloading and caching with 4 workers.
- Direct JPEG, PNG, and WebP decoding; FFmpeg is used as a fallback for frame capture.
- Movie and series metadata:
  - synopsis;
  - poster;
  - backdrop;
  - genre;
  - release date;
  - rating;
  - duration;
  - cast;
  - director;
  - trailer, when provided by the IPTV provider.
- Seasons and episodes.
- Persistent favorites.
- Playback progress and resume support for movies and episodes.
- Aggregated series progress.
- Multiple saved profiles/playlists.
- Xtream passwords are kept outside SQLite.
- Secret Service integration through `secret-tool` when available.
- Persistent mpv process controlled through JSON IPC.
- Native mpv X11 window embedded inside the application through X11 reparenting.
- Player HUD with:
  - play/pause;
  - seeking;
  - timeline;
  - volume;
  - fullscreen;
  - live TV channel switching.
- Network, metadata, and thumbnail work runs outside the main event loop.

# Installation for beginners

This section is for users who simply want to **download, build, and run Blazzing**, even if they have never compiled a program before.

Blazzing is currently distributed as **source code**. During the first installation, your computer needs to compile the application.

Once it has been compiled, you do **not** need to repeat the full installation every time you want to open it.

> Important: the repository has already been renamed to **Blazzing**, but the executable is still currently named `visual-iptv`. This will be renamed internally in a later release.

---

## CachyOS / Arch Linux

### 1. Open a terminal

On KDE, search for:

```text
Konsole
```

### 2. Install the required packages

Copy and paste:

```sh
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl ffmpeg mpv libsecret
```

Press `Enter`.

Your system may ask for your password.

When typing a password in the terminal, **no characters are shown on screen**. This is normal.

### 3. Download Blazzing

Run:

```sh
git clone https://github.com/xoykor/blazzing.git
```

This creates a folder named:

```text
blazzing
```

### 4. Enter the project folder

```sh
cd blazzing
```

### 5. Prepare the build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Wait for the command to finish.

If there is no error, continue to the next step.

### 6. Compile Blazzing

```sh
cmake --build build --parallel
```

The first build may take anywhere from a few seconds to a few minutes depending on your computer.

### 7. Run Blazzing

```sh
./build/visual-iptv
```

If the application window opens, the installation was successful.

---

## Quick installation on CachyOS / Arch Linux

If you are already comfortable using a terminal:

```sh
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl ffmpeg mpv libsecret
git clone https://github.com/xoykor/blazzing.git
cd blazzing
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/visual-iptv
```

---

## Ubuntu / Debian and derivatives

### 1. Install the required packages

```sh
sudo apt update
sudo apt install git build-essential cmake pkg-config libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev libjpeg-dev libpng-dev libwebp-dev libssl-dev ffmpeg mpv libsecret-tools
```

### 2. Download Blazzing

```sh
git clone https://github.com/xoykor/blazzing.git
```

### 3. Enter the project folder

```sh
cd blazzing
```

### 4. Build the application

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### 5. Run it

```sh
./build/visual-iptv
```

---

# Installing with Download ZIP

If you do not want to use Git:

1. open the Blazzing repository on GitHub;
2. click the green **Code** button;
3. click **Download ZIP**;
4. extract the downloaded ZIP file;
5. open a terminal inside the extracted folder;
6. install the dependencies for your Linux distribution;
7. run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/visual-iptv
```

Using `git clone` is recommended because updating the application later is much easier.

---

# How to open Blazzing again

You do **not** need to recompile it every time.

If your terminal is already inside the `blazzing` folder:

```sh
./build/visual-iptv
```

If you are somewhere else:

```sh
cd blazzing
./build/visual-iptv
```

If the folder is inside `Downloads`, for example:

```sh
cd ~/Downloads/blazzing
./build/visual-iptv
```

---

# How to update Blazzing

Enter the project folder:

```sh
cd blazzing
```

Download the latest changes:

```sh
git pull
```

Rebuild:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Run the new version:

```sh
./build/visual-iptv
```

---

# Common problems

## `cmake: command not found`

CMake is not installed.

CachyOS / Arch:

```sh
sudo pacman -S cmake
```

Ubuntu / Debian:

```sh
sudo apt install cmake
```

---

## `git: command not found`

CachyOS / Arch:

```sh
sudo pacman -S git
```

Ubuntu / Debian:

```sh
sudo apt install git
```

---

## `mpv: command not found`

CachyOS / Arch:

```sh
sudo pacman -S mpv
```

Ubuntu / Debian:

```sh
sudo apt install mpv
```

---

## CMake says it cannot find `CMakeLists.txt`

You are probably running the command from the wrong directory.

Check your current directory:

```sh
pwd
```

List the files:

```sh
ls
```

The correct Blazzing project folder must contain:

```text
CMakeLists.txt
```

If you installed it using `git clone`, try:

```sh
cd blazzing
```

Then run the CMake command again.

---

## `./build/visual-iptv: No such file or directory`

This usually means Blazzing has not been compiled yet, or the build failed.

Run:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

If an error appears, look for the **first error message** in the terminal output.

---

## The application opens, but video playback does not work

Check whether mpv is installed:

```sh
mpv --version
```

The current player integration also requires an **X11 session**.

On KDE Plasma, you can check your current session with:

```sh
echo $XDG_SESSION_TYPE
```

The expected result is:

```text
x11
```

---

# Controls

## Catalog

| Key | Action |
|---|---|
| `1` | Live TV |
| `2` | Movies |
| `3` | Series |
| Arrow keys | Move focus |
| `Enter` | Open/play item |
| `F` | Toggle favorite |
| `L` | Open profile/playlist selector |
| `Esc` | Leave episode list or clear search |
| `Ctrl+V` | Paste clipboard |
| `Shift+Insert` | Paste PRIMARY selection |

## Player

| Key | Action |
|---|---|
| `Space` | Play/pause |
| `Esc` / `Backspace` | Return to catalog |
| `F11` | Toggle fullscreen |
| `↑` / `↓` | Volume ±5 |
| `←` / `→` | Seek ±10 s for VOD/episodes; previous/next channel for live TV |

The timeline also supports clicking and dragging on seekable content.

---

# Architecture overview

```text
                 +----------------------+
                 |      X11 / Xlib      |
                 |     src/ui_x11       |
                 +----+----+----+-------+
                      |    |    |
          +-----------+    |    +----------------+
          |                |                     |
   +------v------+   +-----v------+      +-------v--------+
   | providers   |   | SQLite     |      | thumbnails     |
   | Xtream/M3U  |   | persistence|      | worker pool    |
   +-------------+   +------------+      +-------+--------+
                                                  |
                                           +------v------+
                                           | FFmpeg CLI  |
                                           +-------------+

                 +----------------------+
                 | persistent mpv       |
                 | JSON IPC + X11       |
                 +----------------------+
```

mpv remains responsible for the video pipeline.

Blazzing does not copy decoded video frames into the UI. Instead, the native X11 window created by mpv is located using the mpv process PID and reparented into Blazzing's video container.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for details.

---

# Technical requirements

- Linux with an X11 session.
- CMake 3.20 or newer.
- C17-compatible compiler.
- Xlib.
- libcurl.
- json-c.
- SQLite3.
- OpenSSL.
- libjpeg.
- libpng.
- libwebp.
- pthreads.
- `ffmpeg` for fallback thumbnail frame capture.
- `mpv` for playback.
- `secret-tool` is optional but recommended for Secret Service password storage.

---

# Development build

On CachyOS / Arch:

```sh
./scripts/build-cachyos.fish
```

More build information:

[docs/BUILDING.md](docs/BUILDING.md)

---

# Tests

Run the normal test suite with:

```sh
ctest --test-dir build --output-on-failure
```

The current test suite covers:

- core and credential validation;
- Xtream parsing;
- M3U parsing;
- SQLite persistence;
- thumbnails;
- FFmpeg backend;
- mpv / JSON IPC backend;
- sanitization of stream URLs in backend diagnostics.

## AddressSanitizer + UndefinedBehaviorSanitizer

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

---

# Local data and privacy

Database:

```text
~/.local/share/visual-iptv-x11/catalog.db
```

Thumbnail cache:

```text
~/.cache/visual-iptv-x11/thumbnails/
```

Xtream passwords are not stored directly in SQLite.

When available, Blazzing uses Secret Service through `secret-tool`.

See:

[docs/DATA_AND_PRIVACY.md](docs/DATA_AND_PRIVACY.md)

---

# Configuration and diagnostics

See:

[docs/CONFIGURATION.md](docs/CONFIGURATION.md)

Example for Fish shell:

```fish
set -lx VIPTV_MPV_DEBUG 1
./build/visual-iptv 2>&1 | tee /tmp/visual-iptv-mpv-debug.log
```

---

# Repository structure

```text
include/visual_iptv/   API shared between modules
src/app/               application entry point
src/core/              core types, errors, ownership
src/provider/          Xtream Codes and M3U
src/database/          SQLite persistence
src/decoder/           capture abstraction + FFmpeg CLI
src/thumbnails/        scheduler, downloading, decoding and cache
src/player_mpv/        mpv process, JSON IPC and X11 reparenting
src/ui_x11/            user interface and module integration
src/tools/             development utilities
tests/                 tests
packaging/             desktop file
docs/                  technical documentation
```

---

# Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Building](docs/BUILDING.md)
- [Configuration and diagnostics](docs/CONFIGURATION.md)
- [Data and privacy](docs/DATA_AND_PRIVACY.md)
- [Development guide](docs/DEVELOPMENT.md)
- [Contributing](CONTRIBUTING.md)
- [Security](SECURITY.md)

---

# Known limitations / roadmap

The following features are not yet part of the current implementation:

- full EPG;
- graphical audio track selector;
- graphical subtitle selector;
- wide Unicode text rendering through Xft/Fontconfig;
- Xtream catalog pagination/lazy loading;
- advanced cache management UI;
- automatic application updates;
- native Arch/AUR package.

---

# License

MIT. See [LICENSE](LICENSE).
