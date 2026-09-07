# Dados e privacidade

## Arquivos persistentes

Banco SQLite:

```text
~/.local/share/visual-iptv-x11/catalog.db
```

Cache de thumbnails:

```text
~/.cache/visual-iptv-x11/thumbnails/
```

O socket JSON IPC do mpv é temporário e existe apenas durante a execução.

## O que o SQLite armazena

O schema atual possui dados para:

- categorias e itens do catálogo;
- favoritos;
- histórico/estado auxiliar;
- metadados de thumbnails;
- configurações;
- perfis;
- progresso de VOD/episódios;
- progresso agregado de séries;
- metadados ricos de filmes/séries.

Perfis Xtream incluem servidor, servidor alternativo e nome de usuário, mas **não incluem a senha**.

## Senhas

Quando `secret-tool` está disponível, a senha é armazenada e recuperada pelo Secret Service do desktop associada ao ID do perfil. Se o serviço não estiver disponível, o perfil continua salvo sem senha e ela deve ser digitada novamente.

Buffers de senha alocados pelo core/alguns jobs são sobrescritos antes de serem liberados.

## URLs de stream

O processo mpv é persistente. A URL da mídia não é passada em `argv`; ela é enviada pelo socket Unix JSON IPC depois que o processo está ativo.

Mensagens do mpv podem, em alguns casos, repetir a URL de um stream. Antes de guardar texto recente de diagnóstico, o backend substitui trechos reconhecidos como `http://...` ou `https://...` por `[URL ocultada]`.

## Repositório e relatórios de bug

Nunca publique em issues, screenshots, logs ou commits:

- usuário/senha Xtream reais;
- URLs privadas de playlist;
- URLs completas de streams autenticados;
- dumps do Secret Service;
- banco `catalog.db` de uma conta real.

Ao reportar problemas, use endpoints fictícios ou um servidor de teste controlado por você.
