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
