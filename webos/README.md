# Blazzing para LG webOS

Este diretório contém o início do porte do Blazzing para TVs LG como **webOS Web App**.

## Estado deste primeiro corte

Já está preparado para:

- empacotamento como aplicativo webOS `.ipk`;
- navegação por setas, OK e Back (keycode 461);
- teclas de mídia mais comuns;
- M3U/M3U8 remoto, Xtream Codes e limite de resposta herdado do frontend TV;
- QR de pareamento com polling de 5 s e expiração em 45 s;
- TV, filmes, séries, favoritos, perfis locais e retomada;
- catálogo em lotes e carregamento limitado de imagens;
- reprodução pelo elemento HTML5 `<video>`, usando o pipeline de mídia nativo da TV.

A lógica de catálogo é reaproveitada do porte Tizen. O arquivo `js/platform.js`
isola as diferenças de controle remoto e ciclo de vida para evitar duas bases
de frontend divergentes.

## Estrutura

    webos/
    ├── appinfo.json
    ├── index.html
    ├── icon.png
    ├── css/
    │   └── app.css
    ├── js/
    │   ├── app.js
    │   ├── boot.js
    │   ├── net-storage.js
    │   ├── pairing.js
    │   ├── platform.js
    │   ├── player.js
    │   └── providers.js
    └── tests/
        ├── pairing.test.js
        └── providers.test.js

## Empacotar

Com o webOS TV CLI instalado, a partir da raiz do repositório:

    ares-package -e tests -e README.md webos

Ou use o helper em Fish:

    ./scripts/build-webos.fish

O resultado é um arquivo `.ipk`.

## Instalar em uma TV

Depois de configurar a TV em Developer Mode e cadastrar o dispositivo no CLI:

    ares-install --device NOME_DO_DISPOSITIVO com.xoykor.blazzing_0.1.0_all.ipk
    ares-launch --device NOME_DO_DISPOSITIVO com.xoykor.blazzing

O nome exato do IPK pode variar conforme a versão da CLI.

## Próximas validações

Ainda precisam de teste em hardware LG real:

1. codecs e variantes HLS aceitos pelo modelo da TV;
2. comportamento do seek e troca rápida de canal;
3. input de texto pelo teclado virtual;
4. política CORS de playlists/arte em versões antigas do webOS;
5. consumo de memória com catálogos muito grandes;
6. Magic Remote/pointer além da navegação por D-pad.
