# Blazzing

[English](README.md) | **Português (Brasil)**

Cliente IPTV desktop nativo para Linux/X11, escrito em C17, com foco em uma interface visual rápida, suporte a Xtream Codes e M3U e reprodução incorporada com mpv.

> Use o aplicativo somente com listas, servidores e conteúdos que você tenha autorização para acessar.

## Recursos atuais

- TV ao vivo, filmes e séries em catálogos separados.
- Xtream Codes com servidor primário e servidor alternativo para failover.
- Playlists M3U/M3U8 remotas ou locais.
- Grid visual adaptativo para artwork vertical, horizontal e quadrado.
- Download e cache assíncronos de thumbnails com 4 workers.
- Decodificação direta de JPEG, PNG e WebP; FFmpeg é usado como fallback para captura de frames.
- Metadados de filmes e séries: sinopse, capa, backdrop, gênero, lançamento, nota, duração, elenco, direção e trailer quando fornecidos pelo provedor.
- Temporadas e episódios.
- Favoritos persistentes.
- Progresso e retomada de filmes/episódios, incluindo progresso agregado de séries.
- Perfis/listas salvos localmente.
- Senhas Xtream fora do SQLite; integração com Secret Service através de `secret-tool` quando disponível.
- mpv persistente controlado por JSON IPC.
- Janela nativa X11 do mpv incorporada à interface por reparenting.
- HUD com pause, seek, timeline, volume, fullscreen e troca de canais.
- Operações de rede, metadados e thumbnails fora do event loop principal.

# Instalação para iniciantes

Esta seção é para quem só quer **baixar, compilar e abrir o Blazzing**, mesmo sem experiência com programação.

Atualmente o projeto é distribuído como **código-fonte**. Na primeira instalação, seu computador precisa compilar o programa. Isso é feito automaticamente pelos comandos abaixo.

Depois de compilado, você não precisa repetir todo o processo sempre que quiser abrir o aplicativo.

---

## CachyOS / Arch Linux

### 1. Abra o terminal

No KDE, você pode procurar por:

```text
Konsole
```

### 2. Instale as dependências

Copie e cole:

```sh
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl ffmpeg mpv libsecret
```

Pressione `Enter`.

O sistema pode pedir sua senha. Enquanto você digita a senha no terminal, **nenhum caractere aparece na tela**. Isso é normal.

### 3. Baixe o Blazzing

```sh
git clone https://github.com/xoykor/blazzing.git
```

Será criada uma pasta chamada:

```text
blazzing
```

### 4. Entre na pasta

```sh
cd blazzing
```

### 5. Prepare a compilação

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Espere o comando terminar.

Se não aparecer uma mensagem de erro, continue.

### 6. Compile

```sh
cmake --build build --parallel
```

Na primeira vez, isso pode levar de alguns segundos a alguns minutos.

### 7. Abra o programa

```sh
./build/visual-iptv
```

Se a janela abrir, a instalação foi concluída.

---

## Instalação rápida no CachyOS / Arch

Se você já sabe usar o terminal, basta executar:

```sh
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl ffmpeg mpv libsecret
git clone https://github.com/xoykor/blazzing.git
cd blazzing
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/visual-iptv
```

---

## Ubuntu / Debian e derivados

### 1. Instale as dependências

```sh
sudo apt update
sudo apt install git build-essential cmake pkg-config libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev libjpeg-dev libpng-dev libwebp-dev libssl-dev ffmpeg mpv libsecret-tools
```

### 2. Baixe o projeto

```sh
git clone https://github.com/xoykor/blazzing.git
```

### 3. Entre na pasta

```sh
cd blazzing
```

### 4. Compile

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### 5. Abra

```sh
./build/visual-iptv
```

---

## Como abrir novamente depois de instalado

Você **não precisa recompilar toda vez**.

Se o terminal já estiver na pasta `blazzing`:

```sh
./build/visual-iptv
```

Se não estiver:

```sh
cd blazzing
./build/visual-iptv
```

---

## Como atualizar

Entre na pasta:

```sh
cd blazzing
```

Baixe a versão mais recente:

```sh
git pull
```

Recompile:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Abra:

```sh
./build/visual-iptv
```

---

## Instalação usando Download ZIP

Se você não quiser usar Git:

1. clique no botão **Code** no topo da página do GitHub;
2. clique em **Download ZIP**;
3. extraia o ZIP;
4. abra um terminal dentro da pasta extraída;
5. instale as dependências da sua distribuição;
6. execute:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/visual-iptv
```

Usar `git clone` é recomendado porque facilita muito as atualizações.

---

# Problemas comuns

## `cmake: command not found`

O CMake não está instalado.

CachyOS / Arch:

```sh
sudo pacman -S cmake
```

Ubuntu / Debian:

```sh
sudo apt install cmake
```

## `git: command not found`

CachyOS / Arch:

```sh
sudo pacman -S git
```

Ubuntu / Debian:

```sh
sudo apt install git
```

## `mpv: command not found`

CachyOS / Arch:

```sh
sudo pacman -S mpv
```

Ubuntu / Debian:

```sh
sudo apt install mpv
```

## CMake diz que não encontrou `CMakeLists.txt`

Você está executando o comando na pasta errada.

Veja a pasta atual:

```sh
pwd
```

Veja os arquivos nela:

```sh
ls
```

Na pasta correta deve aparecer:

```text
CMakeLists.txt
```

Se você instalou com `git clone`, tente:

```sh
cd blazzing
```

e execute o CMake novamente.

## `./build/visual-iptv: No such file or directory`

Isso normalmente significa que o programa ainda não foi compilado ou que a compilação falhou.

Execute novamente:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Se aparecer erro, procure a **primeira mensagem de erro** no terminal.

## O programa abre, mas o player não funciona

Confira se o mpv está instalado:

```sh
mpv --version
```

A integração atual do player também requer uma sessão gráfica **X11**.

---

## Arquitetura em uma visão

```text
                 +----------------------+
                 |      X11 / Xlib      |
                 |     src/ui_x11       |
                 +----+----+----+-------+
                      |    |    |
          +-----------+    |    +----------------+
          |                |                     |
   +------v------+   +-----v------+      +-------v--------+
   | providers   |   | SQLite     |      | thumbnails     |
   | Xtream/M3U  |   | persistence|      | worker pool    |
   +-------------+   +------------+      +-------+--------+
                                                  |
                                           +------v------+
                                           | FFmpeg CLI  |
                                           +-------------+

                 +----------------------+
                 | persistent mpv       |
                 | JSON IPC + X11       |
                 +----------------------+
```

O mpv continua responsável pelo pipeline de vídeo. O aplicativo não copia frames do player para a interface: a janela X11 nativa criada pelo mpv é encontrada pelo PID e reparentada para o container de vídeo do aplicativo.

Detalhes: [docs/ARCHITECTURE.pt-BR.md](docs/ARCHITECTURE.pt-BR.md).

## Requisitos técnicos

- Linux com sessão X11.
- CMake 3.20 ou superior.
- Compilador com C17.
- Xlib, libcurl, json-c, SQLite3, OpenSSL, libjpeg, libpng, libwebp e pthreads.
- `ffmpeg` para captura de frames de thumbnail.
- `mpv` para reprodução.
- `secret-tool` é opcional, mas recomendado para salvar senhas via Secret Service.

## Build para desenvolvimento

```sh
./scripts/build-cachyos.fish
```

Mais opções: [docs/BUILDING.pt-BR.md](docs/BUILDING.pt-BR.md).

## Testes

```sh
ctest --test-dir build --output-on-failure
```

A suíte atual cobre:

- core e validação de credenciais;
- parsing Xtream;
- M3U;
- SQLite;
- thumbnails;
- backend FFmpeg;
- backend mpv/JSON IPC, incluindo sanitização de URLs nos logs.

Para AddressSanitizer + UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

## Controles

### Catálogo

| Tecla | Ação |
|---|---|
| `1` | TV ao vivo |
| `2` | Filmes |
| `3` | Séries |
| Setas | Mover foco |
| `Enter` | Abrir/reproduzir item |
| `F` | Alternar favorito |
| `L` | Seletor de listas/perfis |
| `Esc` | Voltar da lista de episódios ou limpar pesquisa |
| `Ctrl+V` | Colar clipboard |
| `Shift+Insert` | Colar seleção PRIMARY |

### Player

| Tecla | Ação |
|---|---|
| `Espaço` | Play/pause |
| `Esc` / `Backspace` | Voltar ao catálogo |
| `F11` | Alternar fullscreen |
| `↑` / `↓` | Volume ±5 |
| `←` / `→` | Seek ±10 s em VOD/episódios; canal anterior/próximo em TV ao vivo |

A timeline também aceita clique e arraste em conteúdo seekable.

## Dados locais e privacidade

Banco de dados:

```text
~/.local/share/visual-iptv-x11/catalog.db
```

Cache de thumbnails:

```text
~/.cache/visual-iptv-x11/thumbnails/
```

Senhas Xtream não são gravadas no SQLite. Quando disponível, o aplicativo usa Secret Service através de `secret-tool`.

Veja [docs/DATA_AND_PRIVACY.pt-BR.md](docs/DATA_AND_PRIVACY.pt-BR.md).

## Configuração e diagnóstico

Veja [docs/CONFIGURATION.pt-BR.md](docs/CONFIGURATION.pt-BR.md).

Exemplo em Fish:

```fish
set -lx VIPTV_MPV_DEBUG 1
./build/visual-iptv 2>&1 | tee /tmp/visual-iptv-mpv-debug.log
```

## Documentação

- [Arquitetura](docs/ARCHITECTURE.pt-BR.md)
- [Build e instalação](docs/BUILDING.pt-BR.md)
- [Configuração e diagnóstico](docs/CONFIGURATION.pt-BR.md)
- [Dados e privacidade](docs/DATA_AND_PRIVACY.pt-BR.md)
- [Guia de desenvolvimento](docs/DEVELOPMENT.pt-BR.md)
- [Contribuição](CONTRIBUTING.pt-BR.md)
- [Segurança](SECURITY.pt-BR.md)

## Estrutura do repositório

```text
include/visual_iptv/   API entre módulos
src/app/               entry point
src/core/              tipos, erros e ownership
src/provider/          Xtream Codes e M3U
src/database/          persistência SQLite
src/decoder/           abstração de captura + FFmpeg CLI
src/thumbnails/        scheduler, download, decode e cache
src/player_mpv/        processo mpv, JSON IPC e reparent X11
src/ui_x11/            interface e integração dos módulos
src/tools/             utilitários de desenvolvimento
tests/                 testes
packaging/             arquivo .desktop
docs/                  documentação técnica
```

## Limitações conhecidas / roadmap

Ainda não fazem parte da implementação atual:

- EPG completo;
- seletor gráfico de faixas de áudio;
- seletor gráfico de legendas;
- renderização de texto com cobertura Unicode ampla via Xft/Fontconfig;
- paginação/lazy loading do catálogo Xtream;
- interface avançada de gerenciamento do cache;
- atualização automática;
- pacote nativo AUR/Arch.

## Licença

MIT. Consulte [LICENSE](LICENSE).
