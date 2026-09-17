# Guia de desenvolvimento

O conjunto de recursos da v1.3.0 está congelado. O desenvolvimento nesta branch é apenas de manutenção: bugs, segurança, build e compatibilidade.

[English](DEVELOPMENT.md)

## Ordem recomendada de leitura

1. `include/visual_iptv/core.h`
2. `CMakeLists.txt`
3. `src/ui_x11/x11_app.c`
4. `src/player_mpv/player_mpv.c`
5. `src/provider/xtream.c` e `src/provider/m3u.c`
6. `src/database/database.c`
7. `src/thumbnails/thumbnails.c`
8. `src/decoder/ffmpeg_cli.c`
9. `tests/`

## Ownership

- estruturas `*_init` devem ser liberadas com `*_clear`;
- `*_push` faz deep copy;
- strings heap-owned devem ser liberadas pelo chamador quando documentado;
- `vip_credentials_clear()` sobrescreve a senha antes do `free()`;
- snapshots do player são cópias.

## Threading

- Xlib apenas na thread principal;
- sem rede bloqueante no event loop;
- estado compartilhado deve ter mutex/atomic definido;
- JSON IPC continua serializado pelo `write_mutex`;
- o monitor mpv não deve ser bloqueado por trabalho de UI/rede.

## Player

`video_win` é o container `InputOutput` que recebe a janela mpv. `player_input_win` é um sibling `InputOnly` transparente para mouse/HUD.

O backend final incorpora o mpv por `--wid=<XID do video_win>`. URLs e comandos de reprodução continuam trafegando pelo socket privado de JSON IPC. Não reintroduza o caminho antigo de busca por PID/XReparentWindow: era código legado inalcançável e foi removido na auditoria final.

## Banco

Migrações devem preservar compatibilidade. Dados sensíveis novos não devem ir para SQLite.

## Testes

```sh
ctest --test-dir build --output-on-failure
```

Para memória, concorrência, parsers ou player, rode também os sanitizers.

## Comentários

Comentários devem explicar decisões, invariantes ou protocolos, e não apenas repetir o código.
