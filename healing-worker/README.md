# Blazzing Healing Playlist

Serviço separado do Worker de pareamento.

## Regra principal

As fontes externas **não são snapshots**.

Cada upstream é descrito por:

- repositório;
- branch;
- caminho do arquivo.

O serviço monta o endereço RAW em runtime. Nenhum commit SHA é usado como
origem permanente.

Exemplo lógico:

```
Ramys/Iptv-Brasil-2026
  branch: master
  path: CanaisBR01.m3u8
```

e não:

```
.../raw/<sha-fixo>/CanaisBR01.m3u8
```

O mesmo vale para todas as fontes do Saimo.

## Detecção de atualização

Antes de reprocessar uma fonte, o serviço consulta a API de conteúdo do GitHub e
obtém o blob SHA atual daquele arquivo na branch.

```
branch/path
   |
   +--> SHA igual ao último processado -> mantém catálogo atual
   |
   +--> SHA mudou ---------------------> baixa RAW atual e reindexa
```

Isso evita baixar dezenas de megabytes sem necessidade e, ao mesmo tempo,
garante que uma alteração feita pelo upstream seja incorporada sem recompilar o
Blazzing.

O fetch do RAW é feito com cache de CDN desabilitado no Worker.

## Fontes iniciais

Saimo:

- `catalogo.txt`
- `canais.txt`
- `1.m3u`
- `3.m3u`

Ramys:

- `CanaisBR01.m3u8`
- `CanaisBR02.m3u8`
- `CanaisBR03.m3u8`
- `CanaisBR04.m3u8`
- `Filmes-Series.m3u8` (VOD)

A arquitetura aceita novas fontes sem mudar o cliente.

## Próxima camada

O índice gerado terá múltiplas fontes por item, separando Live e VOD:

```
Live:
Canal
  fonte 1  <- preferida
  fonte 2
  fonte 3

VOD:
Filme/Série
  fonte 1
  fonte 2
  fonte 3
```

A política de reprodução seguirá o comportamento observado no Saimo:

- fonte atual continua preferida enquanto saudável;
- uma falha isolada não provoca troca imediata;
- 3 falhas consecutivas permitem avanço;
- última playlist HLS válida pode segurar um refresh por até 20 s;
- ao confirmar a queda, tenta a próxima fonte;
- nenhuma URL upstream precisa aparecer como URL configurada no Blazzing.

O Blazzing consumirá apenas o endpoint permanente do serviço.
