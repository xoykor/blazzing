# Identidade visual do Blazzing

O porte **Samsung Tizen** é a referência visual canônica do projeto. Linux/X11 e Windows devem adaptar a mesma linguagem ao ambiente nativo sem copiar dependências ou implementação do Tizen.

## Tokens canônicos

| Papel | Valor |
|---|---|
| Fundo | `#090B11` |
| Texto principal | `#F6F7FB` |
| Superfície | `#171C28` |
| Superfície profunda | `#111722` |
| Superfície elevada | `#202839` |
| Card | `#11151F` |
| Campo | `#0D1119` |
| Texto secundário | `#AAB2C4` |
| Texto terciário | `#7F8798` |
| Laranja principal | `#FF5F2E` |
| Laranja quente | `#FF8A45` |
| Foco | `#FFFFFF` |
| Erro/perigo | `#FF7185` |

O antigo azul `#62A9FF` não faz mais parte da identidade principal.

## Componentes

- Marca: quadrado arredondado com “B”, gradiente laranja e texto Blazzing.
- Home: fundo escuro com brilho laranja discreto, hero superior e painéis elevados.
- Botão principal: gradiente `#FF5F2E → #FF8A45`.
- Navegação ativa: mesma família laranja; texto branco.
- Foco: borda branca nítida com realce laranja ao redor.
- Cards: cantos arredondados, superfície `#11151F`, arte na parte superior e metadados abaixo.
- Favorito: controle circular sobre a arte; amarelo quando ativo.
- Catálogo: navegação principal no topo, categorias na lateral e conteúdo à direita.
- QR: modal escuro, QR branco grande e copy lateral.

## Adaptação por plataforma

### Tizen
Fonte de referência. Prioriza distância de visualização, foco por controle remoto e elementos maiores.

### Windows
Mantém a mesma hierarquia do Tizen em escala desktop: hero, painéis, topnav, sidebar, cards e modal QR. Hover não substitui foco; ambos usam a mesma linguagem visual.

### Linux/X11
Mantém a estrutura e hitboxes nativos existentes, mas usa a mesma paleta, proporções gerais, arredondamento e linguagem de foco. Componentes desenhados por Cairo/Xlib devem aproximar os tokens canônicos sem introduzir dependência Web.

## Regra de manutenção

Mudanças visuais novas devem ser definidas primeiro em relação ao Tizen e então adaptadas aos outros portes. O teste `windows/tests/visual-identity.test.js` impede regressões básicas de paleta e hierarquia.
