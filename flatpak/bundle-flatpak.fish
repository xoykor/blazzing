#!/usr/bin/env fish
set app_id io.github.xoykor.Blazzing
set manifest flatpak/$app_id.yml

if not command -q flatpak-builder
    echo "Instale flatpak-builder antes de continuar."
    exit 1
end

flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub org.freedesktop.Platform//25.08 org.freedesktop.Sdk//25.08

rm -rf build-flatpak flatpak-repo
flatpak-builder --repo=flatpak-repo --force-clean build-flatpak $manifest
flatpak build-bundle flatpak-repo Blazzing.flatpak $app_id
echo "Bundle criado em: Blazzing.flatpak"
