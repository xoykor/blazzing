#!/usr/bin/env fish

set script_dir (cd (dirname (status --current-filename)); and pwd)
set version 25
set ares_launch "$script_dir/node_modules/.bin/ares-launch"

if test (count $argv) -ge 1
    set version $argv[1]
end

"$script_dir/prepare.fish"
or exit $status

if not test -x "$ares_launch"
    if type -q ares-launch
        set ares_launch (command -s ares-launch)
    else
        echo "ares-launch não foi encontrado." >&2
        exit 1
    end
end

echo "Abrindo Blazzing no webOS TV Simulator $version..."
"$ares_launch" -s "$version" "$script_dir/app"
