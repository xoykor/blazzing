## 1.4.0 — 2026-09-18

Pareamento pela Internet substitui o pareamento LAN:

- remove o servidor HTTP local de entrada, descoberta de IP LAN, portas de pareamento e necessidade de regra de firewall no PC;
- adiciona protocolo de relay público HTTPS com ID aleatório de sessão de 128 bits e chave AES aleatória de 256 bits;
- cifra nome/URL da playlist no navegador do celular com AES-256-GCM antes do envio ao relay;
- mantém a chave AES no fragmento `#` do QR, remove o fragmento da barra/histórico após abrir a página e nunca armazena a chave no relay;
- mantém apenas IV/ciphertext cifrados e o estado de expiração em um Durable Object SQLite temporário, com expiração em cinco minutos e exclusão explícita após entrega;
- adiciona polling HTTPS verificado e descriptografia ao cliente C e rejeita relay que não seja HTTPS;
- adiciona Cloudflare Worker com Durable Objects SQLite, rate limit por IP para criação de sessões, headers de segurança e smoke tests locais;
- remove hospedagem Oracle/Caddy/systemd e publica o serviço de pareamento diretamente em workers.dev com Wrangler;
- permite embutir o relay de produção com `VIPTV_PAIRING_DEFAULT_URL` e sobrescrever em desenvolvimento com `VIPTV_PAIRING_URL`;
- atualiza a documentação EN/PT-BR atual para a arquitetura via Internet.
- aumenta o intervalo de polling do cliente de 750 ms para 2 segundos, reduzindo bastante o volume de requisições ao Worker/Durable Object sem prejudicar perceptivelmente o envio pelo celular.

## 1.3.3 — 2026-09-17

Correção de acesso ao pareamento pela rede local:

- prioriza endereços IPv4 de Wi-Fi/Ethernet físicos em vez de Docker, Podman, VPN, WireGuard, Tailscale e outras interfaces virtuais ao gerar o QR;
- usa a porta TCP `47831` como porta preferencial para que a regra de firewall permaneça estável entre sessões;
- usa automaticamente outra porta livre se a `47831` já estiver ocupada;
- mostra na interface a porta TCP real que deve ser liberada quando o firewall bloquear conexões de entrada;
- documenta solução para UFW/CachyOS e limitações de Wi-Fi convidado/isolamento de clientes;
- adiciona teste de regressão iniciando dois servidores de pareamento ao mesmo tempo e verificando o fallback para outra porta.

## 1.3.2 — 2026-09-17

Navegação por controle remoto e entrada de listas pelo celular:

- adiciona navegação de foco por setas na tela inicial, perfis salvos, menu superior do catálogo, busca, menu lateral de categorias e grade de mídia;
- aceita Return/KP Enter/Select do X11 como OK e mapeamentos comuns de Back/Escape/Backspace como retorno contextual;
- adiciona servidor HTTP local temporário para pareamento, com porta livre automática, detecção do IP LAN roteado e token aleatório de uso temporário;
- exibe QR Code gerado localmente para colar URLs M3U/M3U8 pelo celular na mesma LAN confiável;
- usa fallback somente localhost quando nenhum IP LAN utilizável é encontrado, evitando QR Code inalcançável;
- reforça a página de pareamento com no-store/no-referrer/CSP/nosniff/frame-deny e valida URLs HTTP/HTTPS;
- adiciona testes de regressão do servidor para envio válido, token inválido, esquema inválido e headers de segurança;
- adiciona `VIPTV_INPUT_DEBUG=1` para identificar mapeamentos incomuns de botões de controles;
- adiciona o crédito discreto `by Xoykor` na tela inicial;
- inclui libqrencode no Flatpak e documenta a dependência e o fluxo por controle remoto.

## 1.3.1 — 2026-09-17

Release de manutenção e legibilidade:

- publica oficialmente em Flatpak a revisão final de manutenção feita após a v1.3.0;
- reformata e reorganiza o código C para facilitar leitura e manutenção, sem ampliar o escopo de recursos congelado;
- adiciona cobertura completa de comentários em funções/protótipos e atualiza a documentação do projeto;
- remove código antigo de reparenting mpv/X11 e a dependência X11 desnecessária da biblioteca do player;
- valida a base mantida com análise estática, sanitizers e testes de estresse.

## 1.3.0 — 2026-09-17

Revisão ampla da renderização visual e das interações:

- introduz uma camada compartilhada de renderização Cairo/Pango com superfícies arredondadas antialiasadas, gradientes e tipografia proporcional UTF-8, mantendo a arquitetura nativa C17/X11;
- torna os cards do catálogo IPTV responsivos à largura disponível e moderniza cursor da busca, abas superiores, navegação lateral, estados de carregamento e metadados dos cards;
- adiciona primitivas reutilizáveis de movimento da UI para transições de hover/foco e rolagem suave, com cobertura de regressão dedicada;
- redesenha o hub inicial com UTF-8 correto, cards modernos de serviço e feedback de hover pelo mouse;
- conclui a modernização do login IPTV, painel de perfis salvos, detalhes/metadados e HUD do player incorporado com o mesmo sistema visual;
- moderniza cabeçalho, grid de canais, estados de carregamento/status e controles do player da Pluto TV, impedindo que cards fiquem escondidos pela barra inferior;
- preserva navegação por teclado/mouse, providers, scheduler de miniaturas e comportamento de reprodução enquanto substitui caminhos visuais antigos baseados em fontes bitmap;
- valida o redesign com testes Release, ASan/UBSan, builds Flatpak e QA automatizado por screenshots em Xvfb a 1600×900.

## 1.2.16 — 2026-09-17

Correção do catálogo M3U e da navegação de séries:

- playlists M3U agora são separadas em TV, Filmes e Séries em vez de colocar todos os itens na aba TV;
- categorias de filmes e séries identificadas pelo `group-title` passam a preencher as abas corretas em fontes M3U;
- episódios de séries M3U são condensados em um único card por série na tela principal de Séries;
- ao abrir uma série M3U, o Blazzing agora mostra primeiro as temporadas e depois apenas os episódios da temporada escolhida;
- a caixa de busca agora possui um estado de foco muito mais evidente, com fundo destacado, barra de acento e cursor;
- o fullscreen do player agora acompanha o estado real do gerenciador de janelas, repete a solicitação EWMH no KDE/XWayland e usa fallback sem bordas ocupando a tela inteira quando necessário;
- Prime Video, Max e Globoplay, que apenas abriam o navegador, foram removidos completamente junto com a API de sessão externa;
- o hub inicial agora mantém somente IPTV/Listas e Pluto TV, ambos reproduzidos nativamente no Blazzing.

## 1.2.15 — 2026-09-17

Atualização de UI/UX e navegação de séries:

- redesenha a interface principal de IPTV com visual azul-escuro, superfícies arredondadas e hierarquia tipográfica mais clara;
- renova login, navegação do catálogo, painel de detalhes/metadados e HUD do player incorporado sem alterar os atalhos existentes de teclado e mouse;
- séries agora abrem primeiro na seleção de temporada e, depois, exibem apenas os episódios da temporada escolhida;
- redesenha o hub de streaming e identifica serviços com DRM como sessões externas abertas no navegador, sem sugerir importação ou login integrado;
- mantém o identificador existente do Secret Service para compatibilidade com senhas já salvas, usando “Blazzing” como novo rótulo visível;
- valida o redesign combinado com a suíte completa de 11 testes.

## 1.2.14 — 2026-09-17

Hardening de confiabilidade, segurança e desempenho das miniaturas:

- o encerramento do scheduler de miniaturas agora para rapidamente em vez de esvaziar toda a fila pendente;
- trocas de provider/perfil descartam trabalhos antigos, enquanto rolagem e filtros comuns continuam aquecendo o cache;
- JPEGs corrompidos passam a ser tratados como falha normal de decodificação, sem permitir que a libjpeg encerre o processo;
- downloads de artwork muito lentos são abortados mais cedo para liberar workers;
- o prefetch em segundo plano prioriza principalmente o catálogo ativo antes de aquecer catálogos ocultos;
- os caminhos de reset e compactação do buffer IPC JSON do mpv foram reforçados;
- a gravação de senhas no Secret Service agora trata corretamente writes parciais e interrupções;
- serviços externos com DRM são apresentados explicitamente como sessões abertas no navegador, sem sugerir login integrado;
- as alterações finais foram validadas em Release, ASan/UBSan, Cppcheck e GCC analyzer.

## 1.2.13 — 2026-09-17

Suporte a playlists M3U grandes:

- aumenta o limite de playlists M3U/M3U8 locais e remotas de 32 MiB para 128 MiB;
- catálogos grandes, como playlists de aproximadamente 79 MiB, agora podem ser carregados diretamente;
- redirecionamentos HTTP continuam suportados para URLs diretas ou encurtadas de playlist;
- mensagens de erro do limite de tamanho agora permanecem sincronizadas com o limite configurado.

## 1.2.12 — 2026-09-08

Descoberta automática do endpoint do provider:

- quando o login Xtream normal retorna HTTP 404, o Blazzing pode consultar as APIs de resolução StreamFire/Spark;
- a decodificação do payload e a extração dos candidatos foram implementadas nativamente em C;
- os endereços retornados são normalizados, deduplicados e validados via `player_api.php`;
- o primeiro servidor validado vira o primário e um segundo servidor validado vira o failover;
- credenciais não são enviadas às APIs de resolução quando o login Xtream normal funciona;
- testes de regressão cobrem decodificação e extração de servidores.

# Changelog

[English](CHANGELOG.md)

## 1.2.11 — 2026-09-07

Atualização para impedir travamentos aparentes no carregamento de miniaturas:

- o prefetch em segundo plano não abre mais streams de itens sem artwork fornecido pelo provider;
- thumbnails geradas a partir do vídeo via FFmpeg ficam restritas a cards visíveis/interativos;
- capturas FFmpeg de miniatura são limitadas a duas simultâneas, preservando workers para downloads de capas;
- rolagem e reconstruções de filtro não destroem mais toda a fila pendente de thumbnails;
- testes de regressão verificam o bloqueio de itens sem artwork no background e a preservação da fila durante mudanças de viewport;
- o prefetch contínuo e agressivo de artwork permanece ativo com a fila limitada já existente.

## 1.2.10 — 2026-09-07

Atualização de confiabilidade do pipeline de miniaturas:

- fila de fundo limitada para impedir acúmulo descontrolado do prefetch;
- prioridade justa entre TV, filmes e séries;
- prefetch contínuo e agressivo sem inundar o provider;
- falha no download da arte não dispara mais fallback caro via FFmpeg;
- gravação atômica do cache com `.tmp` + rename;
- JPEGs corrompidos no cache são descartados e baixados novamente;
- falhas de thumbnails passam a gerar diagnóstico amostrado no stderr.

## 1.2.7 — 2026-09-07

Baseline público inicial, originalmente desenvolvido com o nome provisório Visual IPTV e agora publicado como **Blazzing**:

- Xtream Codes e M3U;
- TV, VOD, séries e episódios;
- navegação visual e artwork adaptativo;
- thumbnails assíncronos;
- favoritos, perfis, metadados e progresso em SQLite;
- mpv persistente por JSON IPC com janela X11 nativa incorporada;
- HUD, timeline, pause/seek/volume/fullscreen e failover;
- suíte de testes para os módulos principais.
