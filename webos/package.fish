#!/usr/bin/env fish

set script_dir (cd (dirname (status --current-filename)); and pwd)
set app_dir "$script_dir/app"
set out_dir "$script_dir/dist"

if not type -q ares-package
    echo "ares-package não foi encontrado. Instale o webOS CLI atual da LG." >&2
    exit 1
end

mkdir -p "$out_dir"

echo "Empacotando Blazzing webOS..."
ares-package -o "$out_dir" "$app_dir"
or exit $status

echo
echo "IPK gerado em:"
find "$out_dir" -maxdepth 1 -type f -name '*.ipk' -print
