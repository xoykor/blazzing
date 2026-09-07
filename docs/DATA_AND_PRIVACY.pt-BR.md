# Dados e privacidade

[English](DATA_AND_PRIVACY.md)

## Arquivos persistentes

Banco SQLite:

```text
~/.local/share/visual-iptv-x11/catalog.db
```

Cache:

```text
~/.cache/visual-iptv-x11/thumbnails/
```

O socket JSON IPC do mpv é temporário.

## SQLite

Armazena catálogo, favoritos, estado auxiliar, thumbnails, configurações, perfis, progresso e metadados. Perfis Xtream incluem servidor, servidor alternativo e usuário, mas **não a senha**.

## Senhas

Quando `secret-tool` está disponível, a senha é armazenada pelo Secret Service do desktop. Sem o serviço, o perfil continua salvo sem senha.

Buffers de senha são sobrescritos antes de serem liberados quando aplicável.

## URLs de stream

A mídia não é passada por `argv` ao mpv; a URL é enviada pelo socket JSON IPC. URLs reconhecidas em mensagens de diagnóstico são ocultadas antes de serem retidas.

## Relatórios de bug

Nunca publique credenciais reais, playlists privadas, URLs autenticadas completas, dumps do Secret Service ou bancos reais da aplicação.
