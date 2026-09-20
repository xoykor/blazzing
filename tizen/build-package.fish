#!/usr/bin/env fish
# SPDX-License-Identifier: MIT
#
# Build and sign the Samsung Tizen package from a completely fresh output.
# This avoids packaging stale files that Tizen RDS can leave at the root of
# .buildResult while placing the current build under Debug/projects/tizen.

set -l script_dir (dirname (status --current-filename))
set -l project_dir (realpath "$script_dir")
set -l repo_dir (realpath "$project_dir/..")
set -l build_root "$project_dir/.buildResult"
set -l package_dir "$build_root/Debug/projects/tizen"

if test (count $argv) -lt 1
    echo "Uso: ./tizen/build-package.fish NOME_DO_CERTIFICADO" >&2
    exit 2
end

set -l certificate "$argv[1]"

if not type -q tizen
    echo "Erro: comando 'tizen' não encontrado." >&2
    exit 1
end

if not type -q unzip
    echo "Erro: comando 'unzip' não encontrado." >&2
    exit 1
end

echo "Limpando build Tizen anterior..."
rm -rf "$build_root"

echo "Compilando projeto..."
tizen build-web -- "$project_dir"
or exit $status

if not test -f "$package_dir/config.xml"
    echo "Erro: build nova não apareceu em $package_dir" >&2
    exit 1
end

set -l source_version (string match -r 'version="([^"]+)"' < "$project_dir/config.xml" | string replace -r '.*version="([^"]+)".*' '$1')
set -l built_version (string match -r 'version="([^"]+)"' < "$package_dir/config.xml" | string replace -r '.*version="([^"]+)".*' '$1')

if test "$source_version" != "$built_version"
    echo "Erro: fonte=$source_version, build=$built_version. Abortando pacote stale." >&2
    exit 1
end

echo "Assinando build $built_version..."
tizen package -t wgt -s "$certificate" -- "$package_dir"
or exit $status

set -l wgt (find "$package_dir" -maxdepth 1 -type f -name '*.wgt' -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -n 1 | string replace -r '^[^ ]+ ')
if test -z "$wgt"
    echo "Erro: nenhum WGT foi gerado em $package_dir" >&2
    exit 1
end

set -l packaged_version (unzip -p "$wgt" config.xml 2>/dev/null | string match -r 'version="([^"]+)"' | string replace -r '.*version="([^"]+)".*' '$1')
if test "$packaged_version" != "$source_version"
    echo "Erro: WGT contém versão $packaged_version, mas a fonte é $source_version." >&2
    exit 1
end

echo "OK: $wgt"
echo "Versão confirmada dentro do WGT: $packaged_version"
