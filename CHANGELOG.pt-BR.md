# Changelog

[English](CHANGELOG.md)

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
