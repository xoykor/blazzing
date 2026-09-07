#!/usr/bin/env fish
# SPDX-License-Identifier: MIT
# Run the current build with verbose mpv/X11 diagnostics.

set -l script_dir (dirname (status --current-filename))
set -l root (realpath "$script_dir/..")

if not test -x "$root/build/visual-iptv"
    echo "build/visual-iptv não encontrado. Execute ./scripts/build-cachyos.fish primeiro." >&2
    exit 1
end

set -lx VIPTV_MPV_DEBUG 1
exec "$root/build/visual-iptv" $argv
