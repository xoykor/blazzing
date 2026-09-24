# Blazzing for Windows

The Windows build is a dedicated desktop client. It no longer copies or patches the Samsung/Tizen application.

## Architecture

- **Electron host**: native Windows window, IPC boundary, networking and local playlist cache.
- **Windows renderer**: its own HTML/CSS/JavaScript UI under `windows/`.
- **Xtream Codes**: live TV, movies, series and episode loading through the provider API.
- **M3U/M3U8**: remote playlist download, persistent raw cache and explicit refresh.
- **mpv**: native player launched as its own Windows window and controlled through a private named pipe.
- **Packaged runtime**: GitHub Actions downloads an x64 mpv build and places it under `resources/mpv` inside the release package.

The Windows frontend does not import files from `tizen/`.

## Run from source

Requirements:

- Windows 10 or newer, x64.
- Node.js 20+.
- mpv available through `VIPTV_MPV_PATH`, `windows/runtime/mpv/mpv.exe`, or system `PATH`.

```powershell
cd windows
npm install
npm start
```

## Build installers locally

Place `mpv.exe` in `windows/runtime/mpv/` first, then:

```powershell
cd windows
npm install
npm run dist
```

Outputs are written to `windows/dist/`: an NSIS installer and a portable executable.

## Release build

The Windows GitHub Actions workflow validates syntax, runs architecture tests, downloads the current x64 mpv runtime, packages both Windows targets, generates SHA-256 files, and uploads the assets to the GitHub release matching `package.json`.

## Current Windows features

- Xtream login and saved profiles;
- M3U/M3U8 profiles and local playlist cache;
- TV, movies and series;
- episode selection by season;
- categories and search;
- favorites;
- VOD/episode resume progress;
- native mpv playback;
- installer and portable package.

## Player controls

- `Esc`: close the player and return to the catalog;
- `Space`: play/pause;
- `Left` / `Right`: seek -10 / +10 seconds;
- `Up` / `Down`: volume;
- `F`: fullscreen.

## Security boundary

The renderer has Node integration disabled and context isolation enabled. Network and filesystem access are only exposed through the preload IPC bridge. Media URLs are sent to mpv over the private named pipe rather than through its process command line.
