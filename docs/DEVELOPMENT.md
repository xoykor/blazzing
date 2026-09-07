# Development guide

[Português (Brasil)](DEVELOPMENT.pt-BR.md)

## Recommended reading order

1. `include/visual_iptv/core.h`
2. `CMakeLists.txt`
3. `src/ui_x11/x11_app.c`
4. `src/player_mpv/player_mpv.c`
5. `src/provider/xtream.c` and `src/provider/m3u.c`
6. `src/database/database.c`
7. `src/thumbnails/thumbnails.c`
8. `src/decoder/ffmpeg_cli.c`
9. `tests/`

## Ownership

The APIs follow explicit C ownership rules:

- structures initialized with `*_init` must be released with `*_clear`;
- `*_push` functions perform deep copies;
- heap-owned strings returned by an API must be `free()`d by the caller when documented;
- `vip_credentials_clear()` overwrites the password before `free()`;
- player snapshots are copies and never expose internal pointers.

## Threading rules

- Do not draw or manipulate Xlib from worker threads.
- Do not run blocking network operations in the event loop.
- When adding shared state, clearly define which mutex or atomic protects it.
- JSON IPC commands must remain serialized through the player's `write_mutex`.
- Do not block the mpv monitor with UI or network work.

## Player

Before changing video embedding, understand the two separate windows:

- `video_win`: `InputOutput` container that receives the native mpv window as a child;
- `player_input_win`: transparent `InputOnly` sibling used for mouse/HUD interaction.

The current backend **does not use `--wid`**. mpv creates its own window; the application discovers it through `_NET_WM_PID` and reparents it. Preserve this contract unless there is a strong architectural reason to replace it.

The media URL must remain outside the mpv process argv.

## UI and jobs

`x11_app.c` is the orchestration layer. Login, seasons, and metadata have dedicated workers. Before adding another worker, check whether the existing scheduler or another asynchronous operation can handle the task.

The grid uses the predominant artwork class (`portrait`, `landscape`, `square`) to determine card geometry and column count. Images preserve aspect ratio; never assume every provider supplies 16:9 thumbnails.

## Database

Migrations must remain compatible with existing databases. Do not silently remove tables or columns. New sensitive data must not be added to SQLite.

## Tests

Always run:

```sh
ctest --test-dir build --output-on-failure
```

For changes involving memory, concurrency, parsers, or the player, also run a sanitizer build.

The mpv test uses a fake Python process that implements the JSON IPC socket. This allows command/event parsing to be tested without a real graphical session.

## Comment style

Comments should explain **why** a decision exists, ownership/threading invariants, or external protocols. Avoid comments that merely restate the C statement immediately below them.

Public headers contain API comments; implementation details belong in the corresponding `.c` file.
