#!/usr/bin/env fish
set app_id io.github.xoykor.Blazzing
set manifest flatpak/$app_id.yml

if not command -q flatpak
    echo "Flatpak não está instalado."
    echo "CachyOS/Arch: sudo pacman -S --needed flatpak"
    exit 1
end

if not command -q flatpak-builder
    echo "flatpak-builder não está instalado."
    echo "CachyOS/Arch: sudo pacman -S --needed flatpak-builder"
    exit 1
end

if not flatpak remotes --user | string match -q 'flathub*'
    flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
end

flatpak install --user -y flathub org.freedesktop.Platform//25.08 org.freedesktop.Sdk//25.08
flatpak-builder --user --install --force-clean build-flatpak $manifest
echo
echo "Instalado. Execute com:"
echo "flatpak run $app_id"
