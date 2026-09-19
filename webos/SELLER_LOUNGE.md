# Blazzing webOS — Seller Lounge submission draft

This document is a preparation draft. It does not replace the current LG Seller
Lounge forms, UX Scenario template, or App Self Checklist.

## Store identity

- App title: **Blazzing**
- Suggested category: **Entertainment**
- App ID: `io.github.xoykor.blazzing`
- Current alpha package version: `0.12.0`
- Vendor: `xoykor`

## Short description — pt-BR

Player para TV com suporte a playlists M3U/M3U8, provedores Xtream e conteúdo
gratuito da Pluto TV, com favoritos, busca e retomada de reprodução.

## Full description — pt-BR

Blazzing é um player para LG webOS criado para navegar e reproduzir conteúdo
compatível diretamente na TV.

O aplicativo permite abrir playlists M3U/M3U8 fornecidas pelo usuário, conectar a
provedores compatíveis com Xtream e acessar catálogos regionais gratuitos da
Pluto TV. A interface foi projetada para controle remoto, com navegação
direcional, busca, favoritos, detalhes de filmes e séries e retomada de
reprodução para conteúdos compatíveis.

O Blazzing não fornece listas privadas nem credenciais de provedores. O usuário é
responsável pelas fontes que adiciona ao aplicativo.

## Short description — en-US

TV player for M3U/M3U8 playlists, compatible Xtream providers, and free Pluto TV
content, with search, favorites, and playback resume.

## Full description — en-US

Blazzing is an LG webOS media player designed for remote-first browsing and
playback.

Users can open their own M3U/M3U8 playlists, connect to compatible Xtream
providers, and browse free regional Pluto TV catalogs. The TV interface includes
directional navigation, search, favorites, movie and series details, and
playback resume for compatible content.

Blazzing does not provide private playlists or provider credentials. Users are
responsible for the sources they add to the application.

## Privacy and credential notes

- Xtream passwords remain in memory only and are cleared from the form after use.
- Remembering an Xtream profile is opt-in and stores only server URL + username.
- Favorites store opaque identifiers and safe display metadata.
- Resume state stores playback positions keyed by opaque identifiers.
- M3U favorites do not persist raw media URLs.
- Pluto session JWTs and signed stream URLs remain in service memory only.
- Artwork cache keys use opaque hashes; source image URLs are not used as
  persistent IndexedDB keys.
- Phone pairing uses encrypted payloads and the TV deletes the pairing session
  after successful pickup.

A final privacy policy URL/text should be prepared before store submission.

## UX Scenario draft

### Scenario A — M3U

1. Launch Blazzing.
2. Select **Abrir playlist**.
3. Enter a valid HTTP/HTTPS M3U or M3U8 URL.
4. Select **Carregar**.
5. Browse categories with Left/Right/Up/Down.
6. Move through catalog windows; page boundaries are crossed automatically.
7. Press OK on a channel/item to open playback.
8. Press Back to return to the catalog.

### Scenario B — Xtream

1. From Home, select **Entrar no provider**.
2. Enter server URL, username and password supplied for QA.
3. Optionally enable remembering server + username.
4. Choose TV ao vivo, Filmes, or Séries.
5. Browse/search the catalog.
6. Open a movie or series detail page.
7. Start playback.
8. Verify resume/failover behavior when applicable.

QA credentials must be supplied through the confidential Seller Lounge test
information fields; do not commit them to the repository.

### Scenario C — Pluto TV

1. From Home, select **Pluto TV**.
2. Choose Live or Filmes e séries.
3. Browse the regional catalog.
4. Open a channel/movie/series.
5. Start playback.
6. Return with Back.

Availability varies by region and current Pluto TV catalog.

### Scenario D — Favorites

1. Focus a catalog card.
2. Press the yellow remote key.
3. Open the **Favoritos** group.
4. Verify the item is present.
5. Toggle it again to remove it.

## Submission assets and blockers

Before submission, verify/provide:

- [ ] Packaged small icon: 80×80 PNG, referenced by `icon`.
- [ ] Packaged large icon: 130×130 PNG, referenced by `largeIcon`.
- [ ] Seller Lounge icon: 400×400 PNG.
- [ ] Required Seller Lounge screenshots/store artwork.
- [ ] Current LG App Self Checklist completed with real results.
- [ ] Current LG UX Scenario document completed.
- [ ] Privacy policy URL/text.
- [ ] QA test credentials where a provider requires authentication.
- [ ] Supported webOS versions based on actual Simulator/TV results.
- [ ] Final CI-produced IPK.

The current `appinfo.json` already declares `requiredACG: []`. Re-evaluate this
before release if new Luna APIs are added.

## Resolution validation

LG distinguishes the graphics resolution from video playback resolution. Before
submission, test the application UI at both:

- 1280×720 graphics layout;
- 1920×1080 graphics layout.

Do not infer video codec or 4K support from successful UI rendering; those
capabilities require representative real-TV playback tests.
