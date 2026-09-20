# Blazzing para Samsung Tizen

Este diretório contém o porte do Blazzing para Samsung Smart TV como **Tizen Web App**.

## Escopo implementado

- interface de TV em HTML/CSS/JavaScript sem framework;
- navegação por controle remoto com setas, OK e Back;
- registro de teclas multimídia quando disponíveis;
- M3U/M3U8 remoto com limite de resposta de 128 MiB;
- classificação de TV, filmes e séries;
- inferência de episódios S01E02, T01E02 e 1x02;
- agrupamento de episódios M3U em séries, mesmo quando vierem de grupos diferentes;
- Xtream Codes com servidor principal e alternativo;
- carregamento sob demanda de TV, VOD e séries Xtream;
- temporadas e episódios via get_series_info;
- busca, categorias, favoritos e perfis locais;
- retomada simples de filmes e episódios;
- AVPlay como backend principal;
- HTML5 video como fallback de desenvolvimento;
- grid carregado em lotes para não inflar o DOM em catálogos muito grandes;
- playlist M3U persistida localmente em IndexedDB;
- reabertura automática da última playlist M3U usada;
- nova transferência da M3U somente pela aquisição inicial ou pelo botão **Atualizar playlist**.

O código Linux/C17 continua independente. O porte Tizen não depende de X11, Cairo, SQLite ou mpv.

## Compatibilidade

O manifesto usa required_version 2.3. O frontend evita frameworks, módulos ES, CSS Grid e custom properties para reduzir a dependência de recursos recentes do navegador da TV.

AVPlay é usado em TV real quando webapis.avplay está disponível. O fallback HTML5 permite abrir a interface em navegador para testes básicos, mas a validação final de codecs e streams precisa ser feita em uma TV Samsung ou no ambiente Tizen/Samsung TV.

## Estrutura

    tizen/
    ├── config.xml
    ├── index.html
    ├── css/
    │   └── app.css
    ├── js/
    │   ├── app.js
    │   ├── net-storage.js
    │   ├── player.js
    │   └── providers.js
    └── tests/
        └── providers.test.js

## Build com Tizen Studio CLI

Pré-requisitos:

- Tizen Studio;
- Web CLI;
- Samsung TV Extension;
- Samsung Certificate Extension;
- um certificate profile configurado.

A partir da raiz do repositório, use de preferência o helper que limpa qualquer
build incremental antiga antes de empacotar:

    fish ./tizen/build-package.fish NOME_DO_CERTIFICADO

Manual, se preferir:

    rm -rf tizen/.buildResult
    tizen build-web -- tizen
    tizen package -t wgt -s NOME_DO_CERTIFICADO -- tizen/.buildResult/Debug/projects/tizen

**Importante:** não empacote diretamente `tizen/.buildResult`. O Tizen CLI/RDS
pode manter arquivos antigos na raiz desse diretório, enquanto a build nova fica
em `tizen/.buildResult/Debug/projects/tizen`. Isso pode gerar um WGT assinado
com código antigo mesmo após um build aparentemente bem-sucedido.

O helper também confere se a versão de `config.xml` dentro do WGT é a mesma
versão do fonte antes de considerar o pacote válido.

Para conferir os dispositivos conectados:

    sdb devices

A instalação em TV exige Developer Mode habilitado na televisão, PC autorizado e certificado Samsung compatível com o alvo.

## Controles

### Catálogo

- Setas: mover o foco.
- OK/Enter: abrir item ou controle.
- Back: voltar.
- TV / Filmes / Séries: trocar catálogo.
- Favoritos: filtrar favoritos.
- Atualizar playlist: baixa novamente a M3U aberta e substitui a cópia local.
- Listas: voltar aos perfis.
- Back no catálogo: permanece na playlist aberta; para trocar de lista use **Listas**.

### Player

- OK / Play-Pause: play/pause.
- Esquerda/Direita em filmes e episódios: seek de 10 segundos.
- Esquerda/Direita em TV ao vivo: canal anterior/próximo dentro do filtro atual.
- Stop: fecha o player.
- Back: volta ao catálogo ou à série.

## Armazenamento

Perfis, favoritos, retomada e o identificador da última playlist aberta usam
localStorage da aplicação Tizen. O conteúdo bruto das playlists M3U usa
IndexedDB, para comportar listas grandes sem depender do limite pequeno do
localStorage.

Depois da primeira aquisição, abrir um perfil M3U ou reiniciar a TV usa a
cópia persistida e **não faz download da playlist**. A M3U só é transferida
novamente ao escolher **Atualizar playlist** no catálogo. Se a cópia persistida
estiver inválida, o aplicativo pede atualização em vez de baixar
automaticamente.

A senha Xtream **não é salva por padrão**. Ela só é persistida quando o usuário marca explicitamente a opção de salvar a senha. Diferentemente do desktop Linux, esta primeira versão não possui integração equivalente ao Secret Service.

## Limitações deste primeiro corte

Ainda não foram portados:

- pareamento por celular via Cloudflare Worker;
- Pluto TV;
- resolução automática de endpoints Xtream do desktop;
- metadados ricos de VOD;
- cache persistente de artwork;
- EPG;
- testes em TV Samsung física.

O objetivo deste corte é manter uma base instalável, navegável pelo controle e capaz de reproduzir streams suportados pela TV, sem acoplar o frontend Tizen ao backend Linux.
