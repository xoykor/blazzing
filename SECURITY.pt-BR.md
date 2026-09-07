# Segurança

[English](SECURITY.md)

## Conteúdo sensível

Não publique credenciais Xtream, URLs privadas de M3U, URLs autenticadas de stream, dumps de Secret Service ou bancos reais da aplicação em issues ou pull requests.

## Relatando uma vulnerabilidade

Se **Private vulnerability reporting** estiver habilitado, prefira esse canal. Caso contrário, não anexe segredos a issues públicas.

## Fronteiras atuais

- senhas Xtream não ficam no SQLite;
- Secret Service é usado quando disponível;
- URLs de mídia vão ao mpv por IPC, não argv;
- logs do mpv passam por sanitização;
- FFmpeg é iniciado via `exec`, sem shell.
