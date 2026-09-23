# Blazzing

<p align="center">
  <img src="assets/blazzing.png" alt="Blazzing" width="220">
</p>

<p align="center">
  <a href="README.md">English</a> · <strong>Português (Brasil)</strong>
</p>

Blazzing é um player IPTV multiplataforma. O cliente desktop Linux é nativo em C17, com X11/XWayland, Cairo/Pango, SQLite e reprodução persistente via mpv; o repositório também contém portes para Samsung Tizen e Windows.

> **Estado do projeto:** a **v1.4.7** é a release atual do desktop Linux. Os portes para Samsung Tizen e Windows são mantidos no mesmo repositório. O porte Windows atualmente é compilável pelo código-fonte/CI e ainda não possui uma release Windows versionada publicada. A entrada de playlist pelo celular usa um relay cifrado via Cloudflare, mantendo a descriptografia local no cliente.

> Use o Blazzing somente com listas, servidores e conteúdos que você tenha autorização para acessar.

## Download

A instalação recomendada é o **Flatpak oficial da v1.4.7** disponível na [release do GitHub](https://github.com/xoykor/blazzing/releases/tag/v1.4.7).

Depois de baixar `Blazzing-v1.4.7-x86_64.flatpak`:

```sh
flatpak install --user ./Blazzing-v1.4.7-x86_64.flatpak
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
- navegação por teclado, mouse e controle remoto com setas/OK;
- entrada de M3U/M3U8 pelo celular através de página local temporária e QR Code;
- processo mpv persistente controlado por JSON IPC;
- pause, seek, timeline, volume, fullscreen e troca de canais ao vivo;
- ajustes de hardware decoding por variáveis de ambiente.

Wrappers de serviços comerciais no navegador não fazem parte do Blazzing. A tela inicial mantém os dois caminhos de reprodução implementados nativamente: IPTV/Listas e Pluto TV.

## Plataformas

### Desktop Linux

A aplicação nativa é voltada a Linux com **X11 ou XWayland**. A interface usa Xlib; o mpv recebe diretamente o container X11 de vídeo do Blazzing por `--wid`, enquanto a URL da mídia é enviada depois pelo socket privado de JSON IPC. O Flatpak atual compila propositalmente o caminho X11 do mpv e não possui renderizador Wayland nativo.

### Samsung Tizen

O repositório também contém um **Tizen Web App** separado em `tizen/`. Ele usa HTML/CSS/JavaScript, navegação por controle remoto e Samsung AVPlay quando disponível. Playlists M3U podem ser persistidas no armazenamento privado do aplicativo e reabertas sem novo download até que o usuário escolha explicitamente **Atualizar playlist**.

O porte Tizen não depende de X11, Cairo, SQLite ou mpv. Consulte [tizen/README.md](tizen/README.md) para build, instalação, armazenamento e limitações atuais.

### Desktop Windows

O porte Windows fica em `windows/`. Ele reaproveita o frontend de catálogo/providers do Tizen, mas substitui as APIs Samsung por uma ponte Electron restrita para rede, armazenamento persistente de playlists e reprodução nativa com mpv. O mpv é embutido pelo HWND do Windows e controlado por um named pipe privado, então URLs de mídia são enviadas por JSON IPC em vez de argumentos do processo.

O build por código-fonte requer atualmente Windows 10 ou superior, Node.js para empacotamento e `mpv.exe` no `PATH`, em um dos caminhos locais documentados ou definido por `VIPTV_MPV_PATH`. Veja [windows/README.md](windows/README.md).

## Controles

### Catálogo

| Tecla | Ação |
| --- | --- |
| `Ctrl+1` | TV ao vivo |
| `Ctrl+2` | Filmes |
| `Ctrl+3` | Séries |
| Setas | Mover entre menu superior, busca, menu lateral e grade do catálogo |
| `Enter` / `Select` | Ativar o controle focado, abrir ou reproduzir |
| `Ctrl+F` | Focar a busca |
| `Ctrl+D` | Alternar favorito do item focado no catálogo |
| `Ctrl+L` | Abrir listas/perfis salvos |
| `Back` / `Esc` / `Backspace` | Voltar; Backspace edita a busca enquanto ela contém texto |
| `Ctrl+V` | Colar clipboard |
| `Shift+Insert` | Colar seleção PRIMARY do X11 |

Na grade do catálogo, `←` na primeira coluna entra no menu lateral de categorias e `↑` na primeira linha entra no menu superior. O menu superior dá acesso a TV, Filmes, Séries, Busca, Favoritos e Listas sem mouse.

### Tela inicial / listas

- `←/→` alterna Xtream/M3U quando o seletor de modo está focado.
- `↑/↓` percorre o formulário.
- `→` a partir do formulário entra em **Suas listas**; `↑/↓` seleciona um perfil salvo, `←` volta e `Enter/Select` abre.
- No modo M3U, foque **Adicionar pelo celular** e pressione `Enter/Select`, ou use `F2` para testar no desktop.
- `Back/Esc` cancela um pareamento por celular ativo.

### Adicionar uma URL M3U/M3U8 pelo celular

1. Abra o modo M3U e selecione **Adicionar pelo celular**.
2. O Blazzing cria uma sessão de **45 segundos** no Cloudflare Worker público e consulta o relay a cada 5 segundos.
3. Escaneie o QR Code no celular. O celular pode estar no Wi-Fi ou nos dados móveis.
4. Cole o nome da lista e a URL M3U/M3U8 e envie.
5. O navegador cifra os dados com AES-256-GCM antes do envio.
6. O Blazzing recebe o payload cifrado por HTTPS, descriptografa localmente,
   apaga a sessão e carrega a lista.

O QR contém um identificador aleatório de sessão de 128 bits e uma chave AES
aleatória de 256 bits. A chave fica após o fragmento `#`, não entra nas
requisições HTTP normais e é removida da barra/histórico do navegador após a
página iniciar. O Worker mantém somente os dados cifrados da sessão em um
Durable Object temporário.

Nenhum servidor HTTP local é aberto. O pareamento não exige que celular e PC
estejam na mesma LAN, não exige exceção no firewall do PC e não depende de
isolamento de clientes do roteador ou CGNAT. Não é necessária VPS.

A URL do Worker pode ser embutida no build de produção com
`VIPTV_PAIRING_DEFAULT_URL`. Em desenvolvimento, `VIPTV_PAIRING_URL` pode
sobrescrevê-la.

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
sudo pacman -S --needed git base-devel cmake pkgconf libx11 curl json-c sqlite libjpeg-turbo libpng libwebp openssl cairo pango qrencode ffmpeg mpv libsecret
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

### Windows

```powershell
git clone https://github.com/xoykor/blazzing.git
cd blazzing\windows
npm install
npm start
```

Para gerar instalador NSIS e pacote portátil, use `npm run dist`. O build Windows usa um `mpv.exe` externo; consulte [windows/README.md](windows/README.md) para os caminhos aceitos e controles.

### Debian / Ubuntu

```sh
sudo apt update
sudo apt install git build-essential cmake pkg-config libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev libjpeg-dev libpng-dev libwebp-dev libssl-dev libcairo2-dev libpango1.0-dev libqrencode-dev ffmpeg mpv libsecret-tools

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
- `VIPTV_INPUT_DEBUG=1` — registra keycodes/keysyms X11 para identificar botões de um controle remoto;
- `VIPTV_MPV_RENDERER=gpu|gpu-next|x11` — caminho gráfico alternativo do mpv;
- `VIPTV_MPV_HWDEC=...` — override de hardware decoding;
- `VIPTV_NO_AUDIO=1` — inicia reprodução sem saída de áudio.

Veja [Configuração e diagnóstico](docs/CONFIGURATION.pt-BR.md).

## Escopo do projeto

O **núcleo do desktop Linux está maduro** e mudanças devem priorizar confiabilidade, segurança, compatibilidade e manutenção. Grandes expansões de funcionalidade no desktop são deliberadamente conservadoras.

Os portes Samsung Tizen e Windows são mantidos separadamente dentro do mesmo repositório. O porte Windows compartilha intencionalmente o frontend de catálogo/providers do Tizen, mantendo a integração nativa de desktop isolada em `windows/`.

Ainda não implementados na interface Linux:

- interface EPG completa;
- seletor gráfico de faixas de áudio;
- seletor gráfico de legendas;
- backend gráfico Wayland nativo;
- atualizador automático;
- pacote Arch/AUR nativo.

Consulte o README específico do Tizen para as limitações dessa plataforma; os dois clientes não possuem paridade total de recursos.

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
tizen/                 Web App para Samsung Tizen
windows/               porte desktop Windows (Electron + IPC do mpv)
worker/                relay cifrado de pareamento
docs/                  documentação técnica
```

## Documentação

- [Arquitetura](docs/ARCHITECTURE.pt-BR.md)
- [Build](docs/BUILDING.pt-BR.md)
- [Configuração e diagnóstico](docs/CONFIGURATION.pt-BR.md)
- [Dados e privacidade](docs/DATA_AND_PRIVACY.pt-BR.md)
- [Desenvolvimento](docs/DEVELOPMENT.pt-BR.md)
- [Flatpak](flatpak/README.md)
- [Porte Tizen](tizen/README.md)
- [Porte Windows](windows/README.md)
- [Worker de pareamento](worker/README.md)
- [Changelog](CHANGELOG.pt-BR.md)

## Licença

GNU General Public License v3.0 (`GPL-3.0-only`). Veja [LICENSE](LICENSE). Avisos MIT anteriores aplicáveis são preservados em [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
