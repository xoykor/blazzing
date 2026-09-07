# Contribuindo

Contribuições são bem-vindas para correções, compatibilidade, testes e novas funcionalidades.

## Antes de enviar um PR

1. Não inclua credenciais, playlists privadas, bancos de dados reais ou URLs autenticadas.
2. Compile com os warnings do projeto habilitados.
3. Rode a suíte completa de testes.
4. Para mudanças em parsing, memória, threading ou player, rode também ASan/UBSan.
5. Atualize a documentação quando alterar comportamento público, atalhos, variáveis de ambiente, banco ou arquitetura.

```fish
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j(nproc)
ctest --test-dir build --output-on-failure
```

Sanitizers:

```fish
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DVIPTV_SANITIZE=ON
cmake --build build-asan -j(nproc)
ctest --test-dir build-asan --output-on-failure
```

## Escopo técnico

A base atual é C17 + Xlib, com mpv persistente/JSON IPC e SQLite. Mudanças arquiteturais grandes devem explicar o ganho concreto e preservar, quando aplicável:

- compatibilidade dos dados persistidos;
- separação de credenciais;
- responsividade do event loop;
- URLs de mídia fora do argv do mpv;
- ownership explícito das estruturas C.

## Commits

Prefira commits pequenos e com objetivo claro. Exemplos:

```text
fix: preserve player focus after mpv reparent
feat: add EPG data model
refactor: split catalog filtering from X11 drawing
test: cover relative M3U URLs
```
