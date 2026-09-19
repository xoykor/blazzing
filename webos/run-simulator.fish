#!/usr/bin/env fish

set script_dir (cd (dirname (status --current-filename)); and pwd)
set version 25

if test (count $argv) -ge 1
    set version $argv[1]
end

if not type -q ares-launch
    echo "ares-launch não foi encontrado. Instale o webOS CLI da LG." >&2
    exit 1
end

"$script_dir/prepare.fish"
or exit $status

echo "Abrindo Blazzing no webOS TV Simulator $version..."
ares-launch -s "$version" "$script_dir/app"
