# Blazzing for Windows

The Windows desktop port reuses the Samsung/Tizen catalog frontend and replaces Samsung-specific APIs with an Electron host. Networking, playlist persistence and playback are handled outside the browser sandbox.

## Runtime architecture

- **Electron**: desktop window, native keyboard/fullscreen handling and IPC boundary.
- **Shared frontend**: the Tizen HTML/CSS/provider/controller code is copied at build time so catalog behavior stays aligned between TV and Windows.
- **mpv**: persistent native player embedded into a dedicated Windows window through `--wid=<HWND>`.
- **JSON IPC**: mpv is controlled through a private Windows named pipe. Stream URLs are sent through that pipe and are not placed in the mpv command line.
- **Playlist cache**: remote M3U/M3U8 files are stored under Electron's per-user application-data directory and reused until **Atualizar playlist** is selected.

## Requirements

- Windows 10 or newer, x64.
- Node.js 20+ only when building from source.
- `mpv.exe` available through one of these locations:
  1. `VIPTV_MPV_PATH`;
  2. `resources/mpv/mpv.exe` beside a packaged build;
  3. `windows/mpv/mpv.exe` during development;
  4. the system `PATH`.

The repository does not redistribute an mpv binary. This keeps the Windows package independent of third-party binary release schedules and licensing bundles.

## Run from source

From PowerShell:

```powershell
cd windows
npm install
npm start
```

From `cmd.exe`:

```bat
cd windows
npm install
npm start
```

The `prepare-app` step copies `../tizen` into a generated `windows/app/` directory and injects the Windows bridge. Do not edit the generated copy.

## Build installers

```powershell
cd windows
npm install
npm run dist
```

Outputs are written to `windows/dist/`:

- NSIS installer;
- portable executable/package produced by electron-builder.

## Controls

Catalog:

- Arrow keys: navigation.
- Enter: activate.
- Ctrl+1 / Ctrl+2 / Ctrl+3: TV / Movies / Series.
- Ctrl+F: search.
- Ctrl+D: favorite the focused card.
- Ctrl+L: saved lists.
- Esc: back/exit according to the current screen.
- F11: fullscreen.

Player:

- Space / Enter: play/pause.
- Left / Right: seek ±10 seconds for VOD/episodes, previous/next channel for live TV.
- Esc: return to the catalog.
- F11: fullscreen.

## Security notes

The renderer has Node integration disabled and uses context isolation. HTTP requests and filesystem writes cross a narrow preload bridge. mpv receives media URLs over its private named pipe, not argv, reducing accidental credential exposure in process listings.

As on Tizen, saving an Xtream password is optional. The current shared profile format uses frontend local storage for that opt-in secret; a future Windows-specific credential-vault backend can replace it without changing provider logic.

## Current scope

The Windows port includes:

- M3U/M3U8;
- Xtream Codes;
- live TV, movies and series;
- categories, search and favorites;
- saved profiles;
- persisted M3U cache and explicit refresh;
- resume/progress;
- Lista card/fallback shards;
- QR pairing;
- mpv playback and fallback sources.

Pluto TV is not yet exposed by the shared web frontend, so Windows currently matches the Tizen feature path rather than the Linux X11 hub's separate Pluto mode.
