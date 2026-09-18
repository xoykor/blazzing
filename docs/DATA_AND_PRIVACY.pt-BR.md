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

## Relay de pareamento pelo celular

A entrada M3U pelo celular usa um relay público HTTPS de curta duração. O
Blazzing gera localmente o identificador da sessão e a chave AES-256. O
navegador cifra nome e URL da playlist com AES-256-GCM antes do envio.

O relay mantém somente IV/ciphertext cifrados em RAM e expira sessões após
cinco minutos. A chave AES viaja no fragmento `#` do QR e não faz parte das
requisições HTTP normais. Depois de abrir a página, o JavaScript remove o
fragmento da URL visível/histórico.

Como o relay entrega esse JavaScript, um frontend de relay malicioso ou
modificado poderia teoricamente capturar o texto puro. Use o relay oficial do
Blazzing ou um relay sob seu controle.
