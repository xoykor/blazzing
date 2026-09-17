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
