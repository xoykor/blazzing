# Blazzing

<p align="center">
  <img src="assets/blazzing.png" alt="Blazzing" width="220">
</p>

<p align="center">
  <a href="README.md">English</a> · <strong>Português (Brasil)</strong>
</p>

Blazzing é um player IPTV nativo para Linux escrito em C17. Ele reúne interface X11/XWayland, renderização Cairo/Pango, reprodução persistente com mpv, carregamento assíncrono de imagens, persistência SQLite, Xtream Codes, M3U/M3U8 e Pluto TV em um único aplicativo desktop.

> **Estado do projeto:** a **v1.3.0 é a versão final de recursos**. O conjunto de funcionalidades está congelado. A **v1.3.1 é uma versão de manutenção** com a revisão final de legibilidade, documentação e limpeza do código.

> Use o Blazzing somente com listas, servidores e conteúdos que você tenha autorização para acessar.

## Download

A instalação recomendada é o **Flatpak oficial da v1.3.1** disponível na [release do GitHub](https://github.com/xoykor/blazzing/releases/tag/v1.3.1).

Depois de baixar `Blazzing-v1.3.1-x86_64.flatpak`:

```sh
flatpak install --user ./Blazzing-v1.3.1-x86_64.flatpak
flatpak run io.github.xoykor.Blazzing
```

A release também inclui um arquivo `.sha256` para conferir a integridade do download.

## O que existe na versão final

O Blazzing oferece:

- reprodução nativa de IPTV/listas e reprodução nativa da Pluto TV;
- autenticação Xtream Codes e catálogos separados de TV, filmes e séries;
- servidor Xtream primário/alternativo e lógica de fallback de endpoint;
- playlists M3U/M3U8 locais ou remotas, incluindo redirecionamentos HTTP;
- separação de itens M3U entre TV, filmes e séries;
- agrupamento inferido de séries M3U para que episódios não apareçam soltos no catálogo;
- navegação primeiro pela temporada e depois pelos episódios;
- busca, categorias e favoritos persistentes;
- perfis/listas salvos;
- progresso e retomada de filmes e episódios;
- progresso agregado de séries;
- metadados de filmes e séries quando fornecidos pelo provedor;
- capas, backdrops, logos e thumbnails capturadas do stream;
- workers assíncronos limitados para thumbnails e cache em disco;
- decode de JPEG, PNG e WebP;
- captura de frame por FFmpeg como fallback;
- cards responsivos;
- superfícies arredondadas antialiasadas e texto UTF-8 proporcional com Cairo/Pango;
- navegação por teclado e mouse;
- processo mpv persistente controlado por JSON IPC;
- pause, seek, timeline, volume, fullscreen e troca de canais ao vivo;
- ajustes de hardware decoding por variáveis de ambiente.

Wrappers de serviços comerciais no navegador não fazem parte do Blazzing. A tela inicial mantém os dois caminhos de reprodução implementados nativamente: IPTV/Listas e Pluto TV.

## Plataforma

O Blazzing é voltado a Linux com **X11 ou XWayland**.

A aplicação usa Xlib. O mpv recebe diretamente o container X11 de vídeo do Blazzing por `--wid`, enquanto a URL da mídia é enviada depois pelo socket privado de JSON IPC. O Flatpak atual compila propositalmente o caminho X11 do mpv e não possui backend Wayland nativo.

## Controles

### Catálogo

| Tecla | Ação |
| --- | --- |
| `1` | TV ao vivo |
| `2` | Filmes |
| `3` | Séries |
| Setas | Mover o foco |
| `Enter` | Abrir ou reproduzir |
| `F` | Alternar favorito |
| `L` | Abrir listas/perfis salvos |
| `Esc` | Voltar ou limpar a pesquisa ativa |
| `Ctrl+V` | Colar clipboard |
| `Shift+Insert` | Colar seleção PRIMARY do X11 |

### Player

| Tecla | Ação |
| --- | --- |
| `Espaço` | Play/pause |
| `Esc` / `Backspace` | Voltar ao catálogo |
| `F11` | Alternar fullscreen |
| `↑` / `↓` | Volume ±5 |
| `←` / `→` | Seek ±10 s em mídia seekable; canal anterior/próximo em TV ao vivo |

A timeline também aceita clique e arraste quando o conteúdo permite seek.

## Compilar pelo código-fonte

O executável ainda mantém o nome histórico `visual-iptv`; o nome do produto é Blazzing e o ID Flatpak é `io.github.xoykor.Blazzing`.

### CachyOS / Arch Linux

```sh
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl cairo pango ffmpeg mpv libsecret
git clone https://github.com/xoykor/blazzing.git
cd blazzing
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/visual-iptv
```

Também existe um helper em Fish:

```fish
./scripts/build-cachyos.fish
```

### Debian / Ubuntu

```sh
sudo apt update
sudo apt install git build-essential cmake pkg-config libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev libjpeg-dev libpng-dev libwebp-dev libssl-dev libcairo2-dev libpango1.0-dev ffmpeg mpv libsecret-tools

git clone https://github.com/xoykor/blazzing.git
cd blazzing
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/visual-iptv
```

## Testes e qualidade de manutenção

Suíte normal:

```sh
ctest --test-dir build --output-on-failure
```

Build com sanitizers:

```sh
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-san --parallel
ctest --test-dir build-san --output-on-failure
```

A base final também foi verificada com GCC `-fanalyzer`, Cppcheck e Clang Static Analyzer. O código segue o perfil `.clang-format` do repositório e comenta intencionalmente tanto lógica complexa quanto helpers óbvios para facilitar manutenção sem precisar deduzir a intenção do código.

## Arquitetura

```text
Blazzing
├── UI X11 + Cairo/Pango
│   ├── login / perfis
│   ├── catálogo / busca / navegação de séries
│   └── HUD do player e overlay de entrada
├── providers
│   ├── Xtream Codes
│   ├── M3U/M3U8
│   └── Pluto TV
├── persistência SQLite
├── scheduler/cache de thumbnails
│   └── captura FFmpeg como fallback
└── mpv persistente
    ├── container X11 por --wid
    └── JSON IPC para mídia e comandos
```

Rede, metadados e thumbnails ficam fora do event loop X11 principal. O mpv continua dono do decode/rendering de vídeo; o Blazzing não copia frames decodificados através da UI.

Veja [Arquitetura](docs/ARCHITECTURE.pt-BR.md) para detalhes de ownership e concorrência.

## Dados locais e privacidade

Banco padrão:

```text
~/.local/share/visual-iptv-x11/catalog.db
```

Cache de thumbnails:

```text
~/.cache/visual-iptv-x11/thumbnails/
```

Senhas Xtream não são salvas no SQLite. Quando `secret-tool` está disponível, o Blazzing usa o Secret Service do desktop. URLs de mídia são enviadas ao mpv por JSON IPC em vez de argumentos do processo, e trechos com formato de URL são ocultados dos diagnósticos retidos do mpv.

Veja [Dados e privacidade](docs/DATA_AND_PRIVACY.pt-BR.md).

## Diagnóstico

Overrides de ambiente destinados ao usuário:

- `VIPTV_MPV_DEBUG=1` — diagnóstico detalhado do player;
- `VIPTV_MPV_RENDERER=gpu|gpu-next|x11` — caminho gráfico alternativo do mpv;
- `VIPTV_MPV_HWDEC=...` — override de hardware decoding;
- `VIPTV_NO_AUDIO=1` — inicia reprodução sem saída de áudio.

Veja [Configuração e diagnóstico](docs/CONFIGURATION.pt-BR.md).

## Escopo congelado

Não existe roadmap de funcionalidades depois da v1.3.0. Não fazem parte do conjunto final:

- interface EPG completa;
- seletor gráfico de faixas de áudio;
- seletor gráfico de legendas;
- backend gráfico Wayland nativo;
- atualizador automático;
- pacote Arch/AUR nativo.

Mudanças futuras de código e novas releases ficam reservadas a manutenção: bugs, segurança, quebra de build ou compatibilidade de plataforma.

## Estrutura do repositório

```text
include/visual_iptv/   APIs entre módulos
src/app/               entry point, hub e aplicação Pluto
src/core/              tipos compartilhados, ownership e validação
src/provider/          providers Xtream, M3U e Pluto
src/database/          persistência SQLite
src/decoder/           abstração de captura e backend FFmpeg
src/thumbnails/        fila, download, decode e cache
src/player_mpv/        backend mpv persistente por JSON IPC
src/ui_x11/            UI X11, renderer Cairo/Pango e movimento da interface
src/tools/             ferramentas de diagnóstico/desenvolvimento
tests/                 testes automatizados
flatpak/               manifesto e metadados Flatpak
docs/                  documentação técnica
```

## Documentação

- [Arquitetura](docs/ARCHITECTURE.pt-BR.md)
- [Build](docs/BUILDING.pt-BR.md)
- [Configuração e diagnóstico](docs/CONFIGURATION.pt-BR.md)
- [Dados e privacidade](docs/DATA_AND_PRIVACY.pt-BR.md)
- [Desenvolvimento](docs/DEVELOPMENT.pt-BR.md)
- [Flatpak](flatpak/README.md)
- [Changelog](CHANGELOG.pt-BR.md)

## Licença

MIT. Veja [LICENSE](LICENSE).
