#!/usr/bin/env python3
from pathlib import Path
import json
import re
import subprocess

ROOT = Path(".")

def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"pattern not found in {path}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))

# ---------------------------------------------------------------------------
# Targeted bug/robustness/readability fixes found by the final static audit.
# ---------------------------------------------------------------------------

replace_once(
    "src/decoder/ffmpeg_cli.c",
    """        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(126);
        if (pipefd[1] != STDOUT_FILENO) close(pipefd[1]);
""",
    """        close(pipefd[0]);
        /* If the pipe already occupies stdout there is nothing to duplicate.
         * Avoiding dup2(fd, fd) also makes ownership explicit to analyzers. */
        if (pipefd[1] != STDOUT_FILENO) {
            if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(126);
            close(pipefd[1]);
        }
"""
)

# mpv is embedded directly through --wid now.  Remove the old PID-search /
# XReparentWindow implementation so future maintainers do not have to reason
# about a runtime path that can never execute.
p = Path("src/player_mpv/player_mpv.c")
text = p.read_text()
text = text.replace('#include <X11/Xatom.h>\n#include <X11/Xlib.h>\n\n', '')
text = text.replace(
    """    unsigned long window_id;
    unsigned long native_window_id;
    int native_window_width;
    int native_window_height;
    bool audio;
""",
    """    unsigned long window_id;
    bool audio;
""",
    1,
)
start = text.index("static bool window_has_pid(")
end = text.index("static void drain_mpv_log(", start)
text = text[:start] + text[end:]
text = text.replace(
    """    /* --wid embeds directly into video_win. Keep the legacy native-window
       synchronizer dormant rather than racing the window manager/reparent path. */
    Display *embed_dpy = NULL;
""",
    "",
    1,
)
text = text.replace("        if (embed_dpy) sync_native_mpv_window(player, embed_dpy, pid);\n", "", 1)
text = text.replace("    if (embed_dpy) XCloseDisplay(embed_dpy);\n", "", 1)
text = text.replace(
    """    player->native_window_id = 0u;
    player->native_window_width = 0;
    player->native_window_height = 0;
""",
    "",
    1,
)

old_consume = """static void consume_ipc(vip_mpv_player_t *player, char *buf, size_t *len) {
    pthread_mutex_lock(&player->mutex);
    int fd = player->ipc_fd;
    pthread_mutex_unlock(&player->mutex);
    if (fd < 0) return;
    for (;;) {
        if (*len + 1u >= VIP_MPV_IPC_BUF_CAP) { *len = 0u; buf[0] = '\\0'; }
        ssize_t n = read(fd, buf + *len, VIP_MPV_IPC_BUF_CAP - *len - 1u);
        if (n > 0) {
            *len += (size_t)n;
            buf[*len] = '\\0';
            size_t start = 0u;
            for (size_t i = 0u; i < *len; ++i) {
                if (buf[i] == '\\n') {
                    buf[i] = '\\0';
                    if (i > start) handle_ipc_line(player, buf + start);
                    start = i + 1u;
                }
            }
            if (start > 0u && start <= *len) {
                size_t remaining = *len - start;
                if (remaining > 0u) memmove(buf, buf + start, remaining);
                *len = remaining;
                buf[remaining] = '\\0';
            }
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
}
"""
new_consume = """static void consume_ipc(vip_mpv_player_t *player, char *buf, size_t *len) {
    pthread_mutex_lock(&player->mutex);
    int fd = player->ipc_fd;
    pthread_mutex_unlock(&player->mutex);
    if (fd < 0) return;

    for (;;) {
        /* A complete line larger than the fixed IPC buffer is discarded rather
         * than allowing stale bytes to be treated as a later JSON message. */
        if (*len >= VIP_MPV_IPC_BUF_CAP - 1u) {
            *len = 0u;
            buf[0] = '\\0';
        }

        size_t writable = VIP_MPV_IPC_BUF_CAP - *len - 1u;
        ssize_t n = read(fd, buf + *len, writable);
        if (n > 0) {
            *len += (size_t)n;
            buf[*len] = '\\0';

            size_t consumed = 0u;
            for (size_t i = 0u; i < *len; ++i) {
                if (buf[i] != '\\n') continue;

                buf[i] = '\\0';
                if (i > consumed) handle_ipc_line(player, buf + consumed);
                consumed = i + 1u;
            }

            /* Keep only the initialized partial line at the end of the buffer. */
            if (consumed != 0u) {
                if (consumed < *len) {
                    size_t remaining = *len - consumed;
                    memmove(buf, buf + consumed, remaining);
                    *len = remaining;
                } else {
                    *len = 0u;
                }
                buf[*len] = '\\0';
            }
            continue;
        }

        if (n < 0 && errno == EINTR) continue;
        break;
    }
}
"""
if old_consume not in text:
    raise SystemExit("consume_ipc body did not match")
text = text.replace(old_consume, new_consume, 1)

# Clang's -Wformat-nonliteral is expected inside this printf-style wrapper.
old_log = """    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
"""
new_log = """    va_list ap;
    va_start(ap, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
    vfprintf(stderr, fmt, ap);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    va_end(ap);
"""
if old_log not in text:
    raise SystemExit("debug_log body did not match")
text = text.replace(old_log, new_log, 1)
p.write_text(text)

# The public contract must describe the direct --wid embedding used by runtime.
replace_once(
    "include/visual_iptv/player_mpv.h",
    """ * mpv owns the video rendering path.  On X11, the backend discovers mpv's
 * native window and reparents it into the UI video container instead of
 * copying decoded frames through the application.
""",
    """ * mpv owns the video rendering path. On X11, the backend passes the UI video
 * container through mpv's --wid option, so decoded frames stay inside mpv
 * instead of being copied through the application.
"""
)
replace_once(
    "include/visual_iptv/player_mpv.h",
    '    unsigned long window_id;    /**< X11 container window used for reparenting. */',
    '    unsigned long window_id;    /**< X11 container passed directly to mpv via --wid. */'
)

# player_mpv no longer calls Xlib itself; only the UI module needs X11.
replace_once(
    "CMakeLists.txt",
    "target_link_libraries(vip_player_mpv PUBLIC vip_core Threads::Threads PkgConfig::JSONC PkgConfig::X11)",
    "target_link_libraries(vip_player_mpv PUBLIC vip_core Threads::Threads PkgConfig::JSONC)"
)

# Fix const-correctness in the temporary season-card model.
replace_once(
    "src/ui_x11/x11_app.c",
    "        const char *series_art = series_metadata.cover_url && series_metadata.cover_url[0]",
    "        char *series_art = series_metadata.cover_url && series_metadata.cover_url[0]"
)

# Expand a dense wheel handler so each branch and clamp is visible at a glance.
p = Path("src/ui_x11/x11_app.c")
text = p.read_text()
wheel_start = text.index("static void handle_wheel(")
wheel_end = text.index("\nstatic int browse_columns(", wheel_start)
new_wheel = r'''static void handle_wheel(app_t *a, int x, int y, int direction) {
    /* The login screen scrolls saved profiles independently of the catalog. */
    if (a->screen == SCREEN_LOGIN) {
        int max_scroll = (int)a->profiles.len - 7;
        if (max_scroll < 0) max_scroll = 0;

        a->profile_scroll += direction;
        if (a->profile_scroll < 0) a->profile_scroll = 0;
        if (a->profile_scroll > max_scroll) a->profile_scroll = max_scroll;
        return;
    }

    /* Other screens either have their own input handling or do not scroll. */
    if (a->screen != SCREEN_BROWSE) return;

    if (x < SIDEBAR_W) {
        /* Wheel events over the sidebar move the category list. */
        int max_scroll = (int)ACTIVE_CATEGORIES(a).len - (category_visible_rows(a) - 1);
        if (max_scroll < 0) max_scroll = 0;

        a->category_scroll += direction * 3;
        if (a->category_scroll < 0) a->category_scroll = 0;
        if (a->category_scroll > max_scroll) a->category_scroll = max_scroll;
    } else {
        /* Wheel events over the main grid animate between bounded row offsets. */
        card_layout_t layout = browse_layout(a);
        int rows = (int)((a->filtered_len + (size_t)layout.cols - 1u) / (size_t)layout.cols);
        int content_h = rows * layout.row_step;
        int max_scroll = content_h - (a->height - TOPBAR_H - 18);
        if (max_scroll < 0) max_scroll = 0;

        int base = a->grid_scroll_animating ? a->grid_scroll_target : a->grid_scroll;
        int step = layout.mode == ART_PORTRAIT ? 220 : 180;
        int target = base + direction * step;
        if (target < 0) target = 0;
        if (target > max_scroll) target = max_scroll;

        a->grid_scroll_target = target;
        a->grid_scroll_last_ms = monotonic_ms();
        a->grid_scroll_animating = target != a->grid_scroll;
        if (a->grid_scroll_animating) a->ui_motion_active = true;
    }

    /* y is intentionally unused: only the horizontal region selects a scroller. */
    (void)y;
}
'''
text = text[:wheel_start] + new_wheel + text[wheel_end:]
p.write_text(text)

# ---------------------------------------------------------------------------
# Readability policy: a stable clang-format profile plus comments for every
# function/prototype that does not already have an adjacent explanation.
# ---------------------------------------------------------------------------

Path(".clang-format").write_text("""BasedOnStyle: LLVM
Language: Cpp
IndentWidth: 4
ContinuationIndentWidth: 4
ColumnLimit: 110
BreakBeforeBraces: Attach
AllowShortBlocksOnASingleLine: Never
AllowShortCaseLabelsOnASingleLine: false
AllowShortFunctionsOnASingleLine: Empty
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
DerivePointerAlignment: false
PointerAlignment: Right
ReflowComments: true
SortIncludes: Never
""")

module_descriptions = {
    "src/app/main.c": "Process entry point that hands control to the graphical hub.",
    "src/app/hub.c": "Startup hub window used to choose the native IPTV or Pluto TV experience.",
    "src/app/pluto_app.c": "Native Pluto TV catalog and playback user interface.",
    "src/core/core.c": "Shared ownership, validation, list and error helpers.",
    "src/database/database.c": "SQLite persistence for profiles, catalog state, favorites, metadata and progress.",
    "src/decoder/decoder.c": "Small decoder facade that selects the configured frame-capture backend.",
    "src/decoder/ffmpeg_cli.c": "FFmpeg subprocess backend used for bounded thumbnail frame capture.",
    "src/player_mpv/player_mpv.c": "Persistent mpv runtime controlled through JSON IPC and embedded through X11 --wid.",
    "src/provider/m3u.c": "Local and HTTP M3U loader/parser for the shared catalog model.",
    "src/provider/m3u_catalog.c": "M3U content classification plus inferred series/season/episode grouping.",
    "src/provider/pluto.c": "Pluto TV bootstrap, channel discovery and stream URL provider.",
    "src/provider/server_resolver.c": "Fallback Xtream endpoint discovery and candidate verification.",
    "src/provider/xtream.c": "Xtream Codes HTTP/JSON client and model conversion layer.",
    "src/thumbnails/thumbnail_policy.c": "UI-facing policy that decides which thumbnail jobs may enter the scheduler.",
    "src/thumbnails/thumbnails.c": "Concurrent thumbnail scheduler, artwork downloader, decoder and disk cache.",
    "src/tools/thumb_probe.c": "Small diagnostic command used to exercise thumbnail generation manually.",
    "src/ui_x11/ui_motion.c": "Reusable time-based motion helpers for hover, focus and smooth scrolling.",
    "src/ui_x11/ui_render.c": "Cairo/Pango rendering primitives shared by the native X11 interfaces.",
    "src/ui_x11/x11_app.c": "Main IPTV X11 application integrating providers, persistence, thumbnails and mpv.",
}

def has_module_header(lines):
    # SPDX may be followed by an existing multi-line module description.
    for line in lines[1:8]:
        if line.strip().startswith("/*") and not line.strip().startswith("/** @"):
            return True
    return False

def add_module_header(path):
    lines = path.read_text().splitlines()
    if not lines or has_module_header(lines):
        return
    rel = path.as_posix()
    if rel.startswith("include/"):
        desc = "Public API contract for " + path.stem.replace("_", " ") + "."
    elif rel.startswith("tests/"):
        desc = "Regression tests for " + path.stem.replace("test_", "").replace("_", " ") + "."
    else:
        desc = module_descriptions.get(rel, "Implementation helpers for this module.")
    insert_at = 1 if lines[0].startswith("/* SPDX-License-Identifier:") else 0
    header = [
        "/*",
        f" * {desc}",
        " *",
        " * Comments intentionally cover straightforward helpers as well as subtle",
        " * behavior so a maintainer can follow intent without reverse-engineering it.",
        " */",
    ]
    lines[insert_at:insert_at] = header
    path.write_text("\n".join(lines) + "\n")

source_files = sorted(
    list(Path("src").rglob("*.c"))
    + list(Path("src").rglob("*.h"))
    + list(Path("include").rglob("*.h"))
    + list(Path("tests").rglob("*.c"))
    + list(Path("tests").rglob("*.h"))
)
for path in source_files:
    add_module_header(path)

def words_from_name(name):
    name = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    return [part for part in name.lower().split("_") if part]

def describe(name, kind):
    parts = words_from_name(name)
    original = name
    if parts and parts[0] == "vip":
        parts = parts[1:]
    if not parts:
        return f"Document the {original} {kind}."

    verb = parts[0]
    subject = " ".join(parts[1:]) or original
    verbs = {
        "add": "Add",
        "append": "Append",
        "build": "Build",
        "clear": "Clear",
        "clamp": "Clamp",
        "close": "Close",
        "compare": "Compare",
        "consume": "Consume",
        "copy": "Copy",
        "create": "Create",
        "decode": "Decode",
        "destroy": "Destroy",
        "draw": "Draw",
        "encode": "Encode",
        "ensure": "Ensure",
        "fetch": "Fetch",
        "find": "Find",
        "free": "Release",
        "get": "Return",
        "handle": "Handle",
        "has": "Return whether",
        "init": "Initialize",
        "is": "Return whether",
        "join": "Join",
        "load": "Load",
        "make": "Create",
        "normalize": "Normalize",
        "open": "Open",
        "parse": "Parse",
        "play": "Play",
        "push": "Append",
        "read": "Read",
        "rebuild": "Rebuild",
        "remove": "Remove",
        "render": "Render",
        "reset": "Reset",
        "resolve": "Resolve",
        "run": "Run",
        "save": "Persist",
        "send": "Send",
        "set": "Set",
        "spawn": "Spawn",
        "start": "Start",
        "stop": "Stop",
        "store": "Store",
        "switch": "Switch",
        "terminate": "Terminate",
        "trim": "Trim",
        "update": "Update",
        "validate": "Validate",
        "write": "Write",
    }
    if verb in verbs:
        sentence = f"{verbs[verb]} {subject}"
    elif verb == "main":
        sentence = "Run this executable's main entry point"
    else:
        sentence = f"Implement the {original} helper"
    return sentence[0].upper() + sentence[1:] + "."

# Ask Universal Ctags for exact function/prototype start lines. JSON output keeps
# multi-line signatures safe; a regex-based source rewriter would be fragile.
cmd = [
    "ctags", "--output-format=json", "--fields=+nK", "--kinds-C=fp", "-o", "-"
] + [str(p) for p in source_files]
proc = subprocess.run(cmd, check=True, text=True, capture_output=True)
by_file = {}
for raw in proc.stdout.splitlines():
    if not raw.strip():
        continue
    tag = json.loads(raw)
    if tag.get("_type") != "tag":
        continue
    kind = tag.get("kind")
    if kind not in ("function", "prototype"):
        continue
    line = int(tag["line"])
    by_file.setdefault(tag["path"], []).append((line, tag["name"], kind))

def previous_nonblank(lines, index):
    i = index - 1
    while i >= 0 and not lines[i].strip():
        i -= 1
    return i

def already_documented(lines, index):
    i = previous_nonblank(lines, index)
    if i < 0:
        return False
    prev = lines[i].strip()
    return (
        prev.startswith("//")
        or prev.endswith("*/")
        or prev.startswith("/*")
        or prev.startswith("*")
    )

for filename, tags in by_file.items():
    p = Path(filename)
    lines = p.read_text().splitlines()
    # Insert from bottom to top so ctags line numbers stay valid.
    for line_no, name, kind in sorted(tags, reverse=True):
        idx = max(0, line_no - 1)
        if already_documented(lines, idx):
            continue
        lines[idx:idx] = [f"/* {describe(name, kind)} */"]
    p.write_text("\n".join(lines) + "\n")

# clang-format turns legacy compressed statements into a consistent,
# human-readable C style without changing behavior.
subprocess.run(["clang-format", "-i", *[str(p) for p in source_files]], check=True)
