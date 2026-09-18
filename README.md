# Blazzing

<p align="center">
  <img src="assets/blazzing.png" alt="Blazzing" width="220">
</p>

<p align="center">
  <strong>English</strong> · <a href="README.pt-BR.md">Português (Brasil)</a>
</p>

Blazzing is a native Linux IPTV player written in C17. It combines an X11/XWayland interface, Cairo/Pango rendering, persistent mpv playback, asynchronous artwork loading, SQLite persistence, Xtream Codes, M3U/M3U8 and Pluto TV in one desktop application.

> **Project status:** **v1.4.0** replaces LAN phone pairing with encrypted Internet pairing through a Cloudflare Worker while keeping playlist decryption local to Blazzing.

> Use Blazzing only with playlists, servers and content that you are authorized to access.

## Download

The recommended installation is the official **v1.4.0 Flatpak bundle** from the [GitHub release](https://github.com/xoykor/blazzing/releases/tag/v1.4.0).

After downloading `Blazzing-v1.4.0-x86_64.flatpak`:

```sh
flatpak install --user ./Blazzing-v1.4.0-x86_64.flatpak
flatpak run io.github.xoykor.Blazzing
```

The release also includes a `.sha256` file for integrity verification.

## Current feature set

Blazzing provides:

- native IPTV/list playback and native Pluto TV playback;
- Xtream Codes authentication and catalogs for live TV, movies and series;
- primary/alternate Xtream server handling and endpoint fallback logic;
- local or remote M3U/M3U8 playlists, including HTTP redirects;
- M3U routing into TV, movies and series;
- inferred M3U series grouping so episode entries appear under one series card;
- season-first navigation, followed by episode selection;
- search, categories and persistent favorites;
- saved profiles/playlists;
- movie and episode resume/progress;
- aggregated series progress;
- movie/series metadata when supplied by the provider;
- poster, backdrop, logo and stream-frame thumbnails;
- bounded asynchronous thumbnail workers and disk cache;
- JPEG, PNG and WebP artwork decoding;
- FFmpeg fallback frame capture;
- responsive catalog cards;
- antialiased rounded surfaces and UTF-8 proportional text through Cairo/Pango;
- keyboard, mouse and TV-remote-friendly arrow/OK navigation;
- phone-assisted M3U/M3U8 entry through a temporary local web page and QR Code;
- a persistent mpv process controlled over JSON IPC;
- pause, seek, timeline, volume, fullscreen and live-channel switching;
- hardware-decoding overrides through environment variables.

Commercial browser wrappers are not part of Blazzing. The home screen contains the two playback paths implemented natively by the application: IPTV/Listas and Pluto TV.

## Platform

Blazzing targets Linux with **X11 or XWayland**.

The application itself uses Xlib. mpv receives Blazzing's X11 video container directly through `--wid`, while media URLs are sent later through the private JSON IPC socket. The current Flatpak intentionally builds the X11 mpv path and does not provide a native Wayland backend.

## Controls

### Catalog

| Key | Action |
| --- | --- |
| `Ctrl+1` | Live TV |
| `Ctrl+2` | Movies |
| `Ctrl+3` | Series |
| Arrow keys | Move between the top menu, search, sidebar and catalog grid |
| `Enter` / `Select` | Activate the focused control, open or play |
| `Ctrl+F` | Focus search |
| `Ctrl+D` | Toggle favorite on the focused catalog item |
| `Ctrl+L` | Open saved lists/profiles |
| `Back` / `Esc` / `Backspace` | Go back; Backspace edits the search while it contains text |
| `Ctrl+V` | Paste clipboard |
| `Shift+Insert` | Paste X11 PRIMARY selection |

From the catalog grid, `←` on the first column enters the category sidebar and `↑` on the first row enters the top menu. The top menu exposes TV, Movies, Series, Search, Favorites and Lists without a mouse.

### Home / playlist screen

- `←/→` changes Xtream/M3U while the mode selector is focused.
- `↑/↓` moves through the form.
- `→` from the form enters **Saved lists**; `↑/↓` selects a saved profile, `←` returns and `Enter/Select` opens it.
- On M3U mode, focus **Add with phone** and press `Enter/Select`, or use `F2` during desktop testing.
- `Back/Esc` cancels an active phone pairing session.

### Add an M3U/M3U8 URL with a phone

1. Open M3U mode and select **Add with phone**.
2. Blazzing creates a five-minute session on the public Cloudflare Worker.
3. Scan the QR Code from the phone. The phone can be on Wi-Fi or mobile data.
4. Paste the playlist name and M3U/M3U8 URL and submit.
5. The browser encrypts the data with AES-256-GCM before sending it.
6. Blazzing receives the encrypted payload over HTTPS, decrypts it locally,
   deletes the session and loads the playlist.

The QR contains a random 128-bit session ID and a random 256-bit AES key. The
key is carried after the URL `#` fragment, is not included in ordinary HTTP
requests, and is removed from the browser address bar/history after bootstrap.
The Worker stores only encrypted session data in a short-lived Durable Object.

No local HTTP server is opened. Pairing does not require the phone and PC to be
on the same LAN, does not require a firewall exception on the PC, and is not
affected by router client isolation or CGNAT. No VPS is required.

The Worker URL can be compiled into production builds with
`VIPTV_PAIRING_DEFAULT_URL`. During development, `VIPTV_PAIRING_URL` can
override it.

### Player

| Key | Action |
| --- | --- |
| `Space` | Play/pause |
| `Esc` / `Backspace` | Return to catalog |
| `F11` | Toggle fullscreen |
| `↑` / `↓` | Volume ±5 |
| `←` / `→` | Seek ±10 s for seekable media; previous/next channel for live TV |

The timeline can also be clicked and dragged on seekable media.

## Build from source

The historical executable name is still `visual-iptv`; the product name and Flatpak ID are Blazzing and `io.github.xoykor.Blazzing`.

### CachyOS / Arch Linux

```sh
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl cairo pango qrencode ffmpeg mpv libsecret
git clone https://github.com/xoykor/blazzing.git
cd blazzing
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/visual-iptv
```

The repository also contains a Fish helper:

```fish
./scripts/build-cachyos.fish
```

### Debian / Ubuntu

```sh
sudo apt update
sudo apt install git build-essential cmake pkg-config libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev libjpeg-dev libpng-dev libwebp-dev libssl-dev libcairo2-dev libpango1.0-dev libqrencode-dev ffmpeg mpv libsecret-tools

git clone https://github.com/xoykor/blazzing.git
cd blazzing
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/visual-iptv
```

## Tests and maintenance quality

Normal suite:

```sh
ctest --test-dir build --output-on-failure
```

Sanitizer build:

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-san --parallel
ctest --test-dir build-san --output-on-failure
```

The final codebase has also been checked with GCC `-fanalyzer`, Cppcheck and Clang Static Analyzer. The source is formatted with the repository `.clang-format` profile and intentionally comments both subtle logic and straightforward helpers so maintenance does not depend on reverse-engineering intent.

## Architecture

```text
Blazzing
├── X11 UI + Cairo/Pango
│   ├── login / profiles
│   ├── catalog / search / series navigation
│   └── player HUD and input overlay
├── providers
│   ├── Xtream Codes
│   ├── M3U/M3U8
│   └── Pluto TV
├── SQLite persistence
├── thumbnail scheduler/cache
│   └── FFmpeg fallback capture
└── persistent mpv
    ├── X11 video container via --wid
    └── JSON IPC for media and commands
```

Network, metadata and thumbnail work is kept off the main X11 event loop. The mpv process owns video decoding/rendering; Blazzing does not copy decoded video frames through the UI.

See [Architecture](docs/ARCHITECTURE.md) for the detailed ownership and concurrency model.

## Local data and privacy

Default database:

```text
~/.local/share/visual-iptv-x11/catalog.db
```

Thumbnail cache:

```text
~/.cache/visual-iptv-x11/thumbnails/
```

Xtream passwords are not stored in SQLite. When `secret-tool` is available, Blazzing uses the desktop Secret Service. Media URLs are sent to mpv over JSON IPC instead of process arguments, and URL-shaped data is redacted from retained mpv diagnostics.

See [Data and privacy](docs/DATA_AND_PRIVACY.md).

## Diagnostics

Supported end-user environment overrides:

- `VIPTV_MPV_DEBUG=1` — detailed player diagnostics;
- `VIPTV_INPUT_DEBUG=1` — log X11 keycodes/keysyms, useful when mapping a TV remote;
- `VIPTV_MPV_RENDERER=gpu|gpu-next|x11` — alternate mpv graphics path;
- `VIPTV_MPV_HWDEC=...` — hardware-decoding override;
- `VIPTV_NO_AUDIO=1` — start playback without audio output.

See [Configuration and diagnostics](docs/CONFIGURATION.md).

## Frozen scope

There is no feature roadmap after v1.3.0. The following are not part of the frozen feature set:

- full EPG interface;
- graphical audio-track selector;
- graphical subtitle selector;
- native Wayland rendering path;
- automatic application updater;
- native Arch/AUR package.

Future code changes and releases are reserved for maintenance: bugs, security, build breakage or platform compatibility.

## Repository layout

```text
include/visual_iptv/   public/internal module APIs
src/app/               entry point, hub and Pluto application
src/core/              shared types, ownership and validation
src/provider/          Xtream, M3U and Pluto providers
src/database/          SQLite persistence
src/decoder/           frame-capture abstraction and FFmpeg backend
src/thumbnails/        queue, download, decode and cache
src/player_mpv/        persistent mpv JSON IPC backend
src/ui_x11/            X11 UI, Cairo/Pango renderer and UI motion
src/tools/             diagnostics/development utilities
tests/                 automated tests
flatpak/               Flatpak manifest and metadata
docs/                  technical documentation
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Building](docs/BUILDING.md)
- [Configuration and diagnostics](docs/CONFIGURATION.md)
- [Data and privacy](docs/DATA_AND_PRIVACY.md)
- [Development](docs/DEVELOPMENT.md)
- [Flatpak](flatpak/README.md)
- [Changelog](CHANGELOG.md)

## License

MIT. See [LICENSE](LICENSE).
