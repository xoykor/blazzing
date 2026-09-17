#!/usr/bin/env python3
from pathlib import Path
import re

FILES = sorted(
    list(Path("src").rglob("*.c"))
    + list(Path("src").rglob("*.h"))
    + list(Path("include").rglob("*.h"))
    + list(Path("tests").rglob("*.c"))
    + list(Path("tests").rglob("*.h"))
)

SPECIAL = {
    "env_enabled": "Return whether the named environment flag is enabled.",
    "debug_log": "Emit a formatted mpv diagnostic only when debug logging is enabled.",
    "monotonic_ms": "Return monotonic time in milliseconds for deadlines and animation timing.",
    "sleep_ms": "Sleep for the requested number of milliseconds, retrying after interruptions.",
    "set_nonblocking": "Put the file descriptor into non-blocking mode when its flags can be read.",
    "is_url_start": "Recognize an HTTP or HTTPS prefix and report its byte length.",
    "url_delimiter": "Return whether a byte terminates a URL inside diagnostic text.",
    "append_char_ring": "Append one byte to the bounded rolling log, dropping oldest data when full.",
    "append_text_ring": "Append text to the bounded rolling diagnostic log.",
    "point_in": "Return whether the supplied point lies inside the rectangle.",
    "hub_color": "Resolve a named X11 color for the startup hub.",
    "jpeg_fail": "Transfer control to the guarded JPEG error path after a libjpeg failure.",
    "fold_accent": "Fold the supported UTF-8 Latin accent byte into a lowercase ASCII letter.",
    "normalize_ascii": "Normalize text to lowercase ASCII words for tolerant catalog classification.",
    "has_word": "Return whether normalized text contains the requested whole word.",
    "fnv1a64_update": "Update an FNV-1a 64-bit hash with the supplied text.",
    "fnv_update": "Update an FNV hash with the supplied text.",
    "fnv_text": "Return a stable FNV hash for the supplied text.",
    "stable_id": "Format a deterministic short identifier derived from text.",
    "curl_write": "Append one libcurl response chunk to the bounded M3U download buffer.",
    "write_response": "Append one libcurl response chunk to the bounded Pluto response buffer.",
    "resolver_write": "Append one resolver HTTP chunk to the bounded response buffer.",
    "main": "Run this executable's main entry point.",
}

VERBS = {
    "init": "Initialize",
    "clear": "Clear owned state from",
    "destroy": "Destroy",
    "close": "Close",
    "open": "Open",
    "save": "Persist",
    "load": "Load",
    "list": "List",
    "get": "Return",
    "set": "Set",
    "touch": "Update the last-used state for",
    "replace": "Replace",
    "update": "Update",
    "push": "Append",
    "append": "Append",
    "add": "Add",
    "remove": "Remove",
    "parse": "Parse",
    "create": "Create",
    "fetch": "Fetch",
    "find": "Find",
    "resolve": "Resolve",
    "collect": "Collect",
    "free": "Release",
    "capture": "Capture",
    "decode": "Decode",
    "encode": "Encode",
    "name": "Return the name of",
    "stop": "Stop",
    "start": "Start",
    "seek": "Seek",
    "switch": "Switch",
    "select": "Select",
    "draw": "Draw",
    "render": "Render",
    "fill": "Fill",
    "stroke": "Stroke",
    "measure": "Measure",
    "layout": "Lay out",
    "handle": "Handle",
    "consume": "Consume",
    "build": "Build",
    "make": "Create",
    "ensure": "Ensure",
    "validate": "Validate",
    "sanitize": "Sanitize",
    "trim": "Trim",
    "reset": "Reset",
    "scale": "Scale",
    "resize": "Resize",
    "copy": "Copy",
    "join": "Join",
    "spawn": "Spawn",
    "run": "Run",
    "authenticate": "Authenticate",
    "classify": "Classify",
    "group": "Group",
    "sort": "Sort",
    "compare": "Compare",
    "count": "Count",
    "format": "Format",
    "write": "Write",
    "read": "Read",
    "send": "Send",
    "toggle": "Toggle",
}

CONTEXT = {
    "database": "database",
    "db": "database",
    "xtream": "Xtream provider",
    "m3u": "M3U provider",
    "pluto": "Pluto provider",
    "mpv": "mpv backend",
    "thumbnail": "thumbnail subsystem",
    "thumbnails": "thumbnail subsystem",
    "ui": "UI",
    "hub": "startup hub",
    "profile": "profile",
    "series": "series catalog",
    "server": "server resolver",
    "streamfire": "server resolver",
}

def words(name):
    name = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    return [x for x in name.lower().split("_") if x and x != "vip"]

def human(parts):
    return " ".join(parts).replace(" mpv ", " mpv ").replace(" m3u ", " M3U ").replace(" xtream ", " Xtream ")

def explain(name):
    if name in SPECIAL:
        return SPECIAL[name]

    parts = words(name)
    for i, token in enumerate(parts):
        if token in ("is", "has", "can"):
            obj = human(parts[i + 1:]) or "the condition"
            ctx = parts[:i]
            suffix = ""
            if ctx:
                suffix = f" for the {CONTEXT.get(ctx[-1], human(ctx))}"
            return f"Return whether {obj}{suffix}."

        if token in VERBS:
            action = VERBS[token]
            obj_parts = parts[i + 1:]
            ctx_parts = parts[:i]
            obj = human(obj_parts) if obj_parts else "the requested state"
            if ctx_parts:
                context = CONTEXT.get(ctx_parts[-1], human(ctx_parts))
                if action == "Return":
                    return f"Return {obj} from the {context}."
                if action in ("Load", "Fetch", "List", "Parse", "Authenticate", "Resolve", "Collect"):
                    return f"{action} {obj} using the {context}."
                return f"{action} {obj} in the {context}."
            return f"{action} {obj}."

    if parts and parts[-1] == "enabled":
        return f"Return whether {human(parts[:-1])} is enabled."
    if parts and parts[-1] in ("worker", "main"):
        return f"Run the {human(parts[:-1])} background worker."
    if parts and parts[-1] == "callback":
        return f"Handle the {human(parts[:-1])} callback."
    return f"Handle the {human(parts)} operation."

pattern = re.compile(r"/\* Implement the ([A-Za-z_][A-Za-z0-9_]*) helper\. \*/")
changed = 0
for path in FILES:
    text = path.read_text()
    def repl(match):
        nonlocal_marker = match.group(1)
        return f"/* {explain(nonlocal_marker)} */"
    new, n = pattern.subn(repl, text)
    if n:
        path.write_text(new)
        changed += n

print(f"Polished {changed} generated function comments.")
