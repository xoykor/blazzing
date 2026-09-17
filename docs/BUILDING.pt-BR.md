# Build e instalação

[English](BUILDING.md)

O Flatpak publicado da v1.3.0 é a instalação recomendada para usuários. Compilar pelo código-fonte é voltado a desenvolvimento, auditoria e manutenção.

## CachyOS / Arch Linux

```sh
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl cairo pango ffmpeg mpv libsecret
```

Compile e teste:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Helper em Fish:

```fish
./scripts/build-cachyos.fish
```

## Debian / Ubuntu

```sh
sudo apt update
sudo apt install git build-essential cmake pkg-config libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev libjpeg-dev libpng-dev libwebp-dev libssl-dev libcairo2-dev libpango1.0-dev ffmpeg mpv libsecret-tools
```

Depois use os mesmos comandos CMake.

## Sanitizers

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-san --parallel
ctest --test-dir build-san --output-on-failure
```

`VIPTV_SANITIZE` habilita AddressSanitizer e UndefinedBehaviorSanitizer com GCC ou Clang.

## Instalação local

```sh
sudo cmake --install build --prefix /usr/local
```

O executável mantém o nome histórico `visual-iptv`. O ID da aplicação Flatpak é `io.github.xoykor.Blazzing`.

## Runtime

Necessário:

- Linux com X11 ou XWayland;
- mpv;
- FFmpeg;
- bibliotecas encontradas pelo CMake.

`secret-tool` é opcional e, quando disponível, fornece armazenamento de senha via Secret Service.

O player passa sua janela filha X11 de vídeo ao mpv com `--wid`; não existe caminho de reprodução Wayland nativo nesta versão.

## Base de compilação

`CMAKE_EXPORT_COMPILE_COMMANDS` fica habilitado. Editores e ferramentas de análise podem usar `build/compile_commands.json`.
