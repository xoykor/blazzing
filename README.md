# Visual IPTV

Cliente IPTV desktop nativo para Linux/X11, escrito em C17, com navegação visual por cards, suporte a Xtream Codes e M3U e reprodução incorporada com mpv.

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

O mpv continua responsável pelo pipeline de vídeo. O aplicativo não copia frames do player para a interface: a janela X11 nativa criada pelo mpv é encontrada pelo PID e reparentada para o container de vídeo do Visual IPTV.

Detalhes: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Requisitos

- Linux com sessão X11.
- CMake 3.20 ou superior.
- Compilador com C17.
- Xlib, libcurl, json-c, SQLite3, OpenSSL, libjpeg, libpng, libwebp e pthreads.
- `ffmpeg` para captura de frames de thumbnail.
- `mpv` para reprodução.
- `secret-tool` é opcional, mas recomendado para salvar senhas via Secret Service.

### CachyOS / Arch Linux

```fish
sudo pacman -S --needed base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl ffmpeg mpv libsecret
```

## Compilar

```fish
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j(nproc)
```

Ou use:

```fish
./scripts/build-cachyos.fish
```

Executar:

```fish
./build/visual-iptv
```

Mais opções de build: [docs/BUILDING.md](docs/BUILDING.md).

## Testes

```fish
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

```fish
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-asan -j(nproc)
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

Senhas Xtream não são gravadas no SQLite. Quando disponível, o aplicativo usa Secret Service através de `secret-tool`. URLs de stream são enviadas ao mpv pelo socket IPC privado e são removidas de mensagens de diagnóstico mantidas pelo backend.

Veja [docs/DATA_AND_PRIVACY.md](docs/DATA_AND_PRIVACY.md).

## Configuração e diagnóstico

As opções de runtime são feitas por variáveis de ambiente, principalmente para diagnóstico e compatibilidade do mpv. Veja [docs/CONFIGURATION.md](docs/CONFIGURATION.md).

Exemplo:

```fish
set -lx VIPTV_MPV_DEBUG 1
./build/visual-iptv 2>&1 | tee /tmp/visual-iptv-mpv-debug.log
```

## Documentação

- [Arquitetura](docs/ARCHITECTURE.md)
- [Build e instalação](docs/BUILDING.md)
- [Configuração e diagnóstico](docs/CONFIGURATION.md)
- [Dados e privacidade](docs/DATA_AND_PRIVACY.md)
- [Guia de desenvolvimento](docs/DEVELOPMENT.md)
- [Publicando no GitHub](docs/GITHUB_PUBLISHING.md)
- [Contribuição](CONTRIBUTING.md)
- [Segurança](SECURITY.md)

Os headers públicos em `include/visual_iptv/` possuem comentários de API e ownership; os arquivos de implementação documentam as decisões menos óbvias de concorrência, IPC, X11, cache e persistência.

## Estrutura do repositório

```text
include/visual_iptv/   API interna/publicável entre módulos
src/app/               entry point
src/core/              tipos, erros e ownership
src/provider/          Xtream Codes e M3U
src/database/          persistência SQLite
src/decoder/           abstração de captura + FFmpeg CLI
src/thumbnails/        scheduler, download, decode e cache
src/player_mpv/        processo mpv, JSON IPC e reparent X11
src/ui_x11/            interface e integração dos módulos
src/tools/             utilitários de desenvolvimento
tests/                 testes unitários/integrados locais
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
