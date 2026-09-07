# Guia de desenvolvimento

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

As APIs seguem C explícito:

- estruturas inicializadas com `*_init` devem ser liberadas com `*_clear`;
- funções `*_push` fazem deep copy;
- strings retornadas como heap-owned devem ser `free()` pelo chamador quando documentado;
- `vip_credentials_clear()` sobrescreve a senha antes do `free()`;
- snapshots do player são cópias e não expõem ponteiros internos.

## Regras de threading

- Não desenhar nem manipular Xlib a partir de workers.
- Não executar rede bloqueante no event loop.
- Ao adicionar estado compartilhado, definir claramente qual mutex/atomic o protege.
- Comandos JSON IPC devem continuar serializados pelo `write_mutex` do player.
- Não bloquear o monitor mpv com trabalho de UI ou rede.

## Player

Antes de alterar incorporação de vídeo, entender duas janelas diferentes:

- `video_win`: container `InputOutput` que recebe a janela nativa mpv como filha;
- `player_input_win`: sibling `InputOnly`, transparente, usado para mouse/HUD.

O backend atual **não usa `--wid`**. O mpv cria sua janela e ela é descoberta por `_NET_WM_PID` e reparentada. Preservar esse contrato evita acoplar o aplicativo ao rendering de frames.

A URL de mídia deve continuar fora do argv do processo mpv.

## UI e jobs

`x11_app.c` é a camada de orquestração. Login, temporadas e metadados possuem workers próprios. Antes de criar outro worker, considere se o scheduler existente ou uma operação já assíncrona pode atendê-lo.

O grid usa a classe predominante de artwork (`portrait`, `landscape`, `square`) para determinar card e colunas. Imagens preservam aspect ratio; não assumir que todo provider entrega thumbnails 16:9.

## Banco

Migrações devem ser compatíveis com bancos já existentes. Não remova tabelas/colunas silenciosamente. Novos dados sensíveis não devem ser adicionados ao SQLite.

## Testes

Execute sempre:

```fish
ctest --test-dir build --output-on-failure
```

Para alterações de memória, concorrência, parser ou player, execute também build com sanitizers.

O teste do mpv usa um processo Python falso que implementa o socket JSON IPC. Assim, comandos e parsing de eventos podem ser testados sem uma sessão gráfica real.

## Estilo de comentários

Comentários devem explicar **por que** uma decisão existe, invariantes de ownership/threading ou protocolos externos. Evite comentários que apenas repetem a instrução C imediatamente abaixo.

Headers públicos possuem comentários de API; detalhes de implementação ficam no `.c` correspondente.
