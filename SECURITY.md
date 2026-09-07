# Segurança

## Conteúdo sensível

Não publique credenciais Xtream, URLs privadas de M3U, URLs autenticadas de stream, dumps de Secret Service ou bancos reais da aplicação em issues ou pull requests.

Os testes do repositório devem usar apenas dados fictícios/controlados.

## Relatando uma vulnerabilidade

Se o repositório estiver com **Private vulnerability reporting** habilitado no GitHub, prefira esse canal para falhas que possam expor credenciais ou permitir execução indevida de código. Caso contrário, não anexe segredos a um issue público; descreva apenas o impacto e o componente afetado até que um canal privado seja definido pelo mantenedor.

## Fronteiras atuais

- Senhas Xtream não são persistidas no SQLite.
- Secret Service é usado quando disponível.
- URLs de mídia são enviadas ao mpv por IPC e não por argv.
- Logs recentes do mpv passam por sanitização de URLs.
- FFmpeg é iniciado via `exec`, sem shell.
