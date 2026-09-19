#!/usr/bin/env fish

set script_dir (cd (dirname (status --current-filename)); and pwd)
set app_dir "$script_dir/app"
set service_dir "$script_dir/service/io.github.xoykor.blazzing.network"
set out_dir "$script_dir/dist"
set ares_package "$script_dir/node_modules/.bin/ares-package"

"$script_dir/prepare.fish"
or exit $status

cd "$script_dir"
npm run validate-release
or exit $status

if not test -x "$ares_package"
    if type -q ares-package
        set ares_package (command -s ares-package)
    else
        echo "ares-package não foi encontrado." >&2
        exit 1
    end
end

mkdir -p "$out_dir"

# Evita que pacotes/checksums de versões anteriores confundam a validação.
find "$out_dir" -maxdepth 1 -type f \( -name '*.ipk' -o -name 'SHA256SUMS.txt' \) -delete

echo "Empacotando Blazzing webOS + serviço de rede..."
"$ares_package" -o "$out_dir" "$app_dir" "$service_dir"
or exit $status

echo
echo "IPK gerado em:"
find "$out_dir" -maxdepth 1 -type f -name '*.ipk' -print

npm run verify-package
or exit $status

echo
echo "Checksum:"
cat "$out_dir/SHA256SUMS.txt"
