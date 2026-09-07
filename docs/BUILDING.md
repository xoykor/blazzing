# Build e instalação

## CachyOS / Arch Linux

Dependências:

```fish
sudo pacman -S --needed base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl ffmpeg mpv libsecret
```

Build Release:

```fish
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j(nproc)
```

Testes:

```fish
ctest --test-dir build --output-on-failure
```

Atalho equivalente:

```fish
./scripts/build-cachyos.fish
```

## Debian / Ubuntu

Os nomes de pacote variam entre versões, mas uma base típica é:

```text
build-essential
cmake
pkg-config
libx11-dev
libcurl4-openssl-dev
libjson-c-dev
libsqlite3-dev
libjpeg-dev
libpng-dev
libwebp-dev
libssl-dev
ffmpeg
mpv
libsecret-tools
```

Depois use os mesmos comandos CMake.

## Sanitizers

```fish
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-asan -j(nproc)
ctest --test-dir build-asan --output-on-failure
```

`VIPTV_SANITIZE` ativa AddressSanitizer e UndefinedBehaviorSanitizer em GCC/Clang.

## Instalação local

Após compilar:

```fish
sudo cmake --install build --prefix /usr/local
```

O CMake instala:

- `visual-iptv` em `bin`;
- `packaging/visual-iptv.desktop` em `share/applications`.

Para desenvolvimento, prefira executar diretamente `./build/visual-iptv`.

## `compile_commands.json`

O projeto define `CMAKE_EXPORT_COMPILE_COMMANDS=ON`; o arquivo fica dentro do diretório de build. Editores e ferramentas como clangd podem apontar para `build/compile_commands.json`.

## Requisitos de runtime

A compilação pode localizar `mpv`, mas o player é uma dependência de runtime. O aplicativo espera uma sessão X11 funcional e um mpv capaz de criar uma janela X11 nativa.
