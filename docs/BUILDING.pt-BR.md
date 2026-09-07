# Build e instalação

[English](BUILDING.md)

## CachyOS / Arch Linux

```sh
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl ffmpeg mpv libsecret
```

Build Release:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Testes:

```sh
ctest --test-dir build --output-on-failure
```

Atalho:

```fish
./scripts/build-cachyos.fish
```

## Debian / Ubuntu

```sh
sudo apt update
sudo apt install git build-essential cmake pkg-config libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev libjpeg-dev libpng-dev libwebp-dev libssl-dev ffmpeg mpv libsecret-tools
```

Depois use os mesmos comandos CMake.

## Sanitizers

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## Instalação local

```sh
sudo cmake --install build --prefix /usr/local
```

Atualmente o CMake instala `visual-iptv` em `bin` e o arquivo `.desktop` em `share/applications`.

Para desenvolvimento, prefira `./build/visual-iptv`.

## Requisitos de runtime

A integração atual exige sessão X11 funcional e mpv capaz de criar uma janela X11 nativa.
