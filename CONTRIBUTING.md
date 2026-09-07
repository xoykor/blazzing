# Contributing

[Português (Brasil)](CONTRIBUTING.pt-BR.md)

Contributions are welcome for bug fixes, compatibility improvements, tests, and new features.

## Before opening a pull request

1. Do not include credentials, private playlists, real databases, or authenticated URLs.
2. Build with project warnings enabled.
3. Run the full test suite.
4. For parser, memory, threading, or player changes, also run ASan/UBSan.
5. Update documentation when public behavior, shortcuts, environment variables, database layout, or architecture changes.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Sanitizers:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## Technical scope

The current codebase is C17 + Xlib, with persistent mpv/JSON IPC and SQLite. Large architectural changes should explain the concrete benefit and preserve, where applicable:

- persisted-data compatibility;
- credential separation;
- event-loop responsiveness;
- media URLs outside mpv argv;
- explicit ownership of C structures.

## Commits

Prefer small commits with one clear purpose.

```text
fix: preserve player focus after mpv reparent
feat: add EPG data model
refactor: split catalog filtering from X11 drawing
test: cover relative M3U URLs
```
