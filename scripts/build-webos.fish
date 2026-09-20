#!/usr/bin/env fish
# SPDX-License-Identifier: MIT
# Empacota o porte LG webOS do Blazzing usando o webOS TV CLI.

set repo_root (realpath (dirname (status filename))/..)
set app_dir "$repo_root/webos"
set out_dir "$repo_root/build/webos"

if not type -q ares-package
    echo "Erro: ares-package não foi encontrado no PATH."
    echo "Instale/configure o webOS TV CLI e tente novamente."
    exit 1
end

mkdir -p "$out_dir"

echo "Empacotando $app_dir ..."
ares-package -o "$out_dir" -e tests -e README.md "$app_dir"
or exit $status

echo
echo "Pacote(s) gerado(s):"
find "$out_dir" -maxdepth 1 -type f -name '*.ipk' -print
