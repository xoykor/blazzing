# Flatpak

App ID:

```text
io.github.xoykor.Blazzing
```

The manifest uses `org.freedesktop.Platform//25.08`, builds mpv 0.41.0
inside the application, and enables the `codecs-extra` extension.

## CachyOS / Arch Linux

Install the build tools:

```fish
sudo pacman -S --needed flatpak flatpak-builder
```

Build and install for the current user:

```fish
./flatpak/build-flatpak.fish
```

Run:

```fish
flatpak run io.github.xoykor.Blazzing
```

## Create a distributable `.flatpak` bundle

```fish
./flatpak/bundle-flatpak.fish
```

The resulting file is:

```text
Blazzing.flatpak
```

A user can install it with:

```fish
flatpak install --user ./Blazzing.flatpak
```

## Permissions

Blazzing currently needs:

- X11 + shared IPC because mpv's native X11 window is reparented into the UI;
- network access for IPTV providers and artwork;
- DRI for GPU rendering/hardware acceleration;
- PulseAudio socket for audio;
- `org.freedesktop.secrets` to store passwords through Secret Service;
- read-only home access for local M3U/M3U8 paths.

The home permission can be narrowed in the future if a portal-based local playlist
picker is implemented.

## GitHub Actions

`.github/workflows/flatpak.yml` builds an x86_64 bundle automatically and uploads
`Blazzing-x86_64.flatpak` as a workflow artifact.
