/* SPDX-License-Identifier: MIT */
"use strict";

global.window = {};

require("../js/providers.js");

var providers = global.window.BlazzingProviders;

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

assert(providers.classifyGroup("Séries e Animes") === "series",
    "grupo de séries deveria ser classificado como series");
assert(providers.classifyGroup("Filmes HD") === "vod",
    "grupo de filmes deveria ser classificado como vod");
assert(providers.classifyGroup("Notícias") === "live",
    "grupo genérico deveria ser live");
assert(providers.cleanCategoryName("BR | FILMES | Ação", "vod") === "Ação",
    "prefixos redundantes de categoria deveriam ser removidos");
assert(providers.cleanCategoryName("CANAIS | Esportes", "live") === "Esportes",
    "prefixo de canais deveria ser removido");
assert(providers.inferKind("Sem grupo", "Filme", "https://example.com/a.mp4", null) === "vod",
    "arquivo de vídeo direto sem grupo deveria ser inferido como VOD");

var episode = providers.parseEpisodeLabel("Loki 2021 S01E02");
assert(episode && episode.title === "Loki", "ano deveria ser removido do título da série");
assert(episode.season === 1 && episode.episode === 2,
    "S01E02 deveria produzir temporada 1 episódio 2");

var mixed = [
    "#EXTM3U",
    "#EXTINF:-1 group-title=\"Séries A\",Loki 2021 S01E01",
    "https://example.com/loki-s01e01.m3u8",
    "#EXTINF:-1 group-title=\"Séries B\",Loki S02E01",
    "https://example.com/loki-s02e01.m3u8",
    "#EXTINF:-1 group-title=\"Notícias\",News, HD",
    "https://example.com/news.m3u8"
].join("\n");

var catalogs = providers.parseM3u(mixed, "https://example.com/list.m3u8");

assert(catalogs.series.items.length === 1,
    "episódios da mesma série em grupos diferentes deveriam ser agrupados");
assert(catalogs.series.items[0].episodes.length === 2,
    "série agrupada deveria conter os dois episódios");
assert(catalogs.live.items.length === 1,
    "canal de notícias deveria permanecer em live");
assert(catalogs.live.items[0].name === "News, HD",
    "vírgula no título EXTINF deve ser preservada");

var duplicatedCategories = [
    "#EXTM3U",
    "#EXTINF:-1 group-title=\"FILMES | Ação\",Filme A",
    "https://example.com/a.mp4",
    "#EXTINF:-1 group-title=\"VOD - Ação\",Filme B",
    "https://example.com/b.mp4"
].join("\n");
var merged = providers.parseM3u(
    duplicatedCategories,
    "https://example.com/list.m3u8"
);
assert(merged.vod.categories.length === 1,
    "categorias equivalentes deveriam ser mescladas");
assert(merged.vod.categories[0].name === "Ação",
    "categoria mesclada deveria usar nome limpo");

console.log("Tizen provider tests: OK");


var withCards = [
    "#EXTM3U",
    "#EXT-X-LISTA-CARDS:https://raw.example/cards",
    "#EXT-X-LISTA-CARDS-VERSION:abc123",
    "#EXT-X-LISTA-CARDS-SHARD-LEN:2",
    "#EXTINF:-1 group-title=\"Filmes | Ação\",Filme X",
    "https://example.com/x.mp4"
].join("\n");
var cardCatalog = providers.parseM3u(
    withCards,
    "https://example.com/list.m3u8"
);
var cardItem = cardCatalog.vod.items[0];
assert(cardItem.cardKey === providers.cardLookupKey("Filme X", "Filmes | Ação"),
    "item deveria preservar cardKey para lookup externo");
assert(cardItem.cardIndexBase === "https://raw.example/cards",
    "base do índice de cards deveria ser preservada");
assert(cardItem.cardIndexVersion === "abc123",
    "versão do índice de cards deveria ser preservada");

assert(cardItem.cardIndexShardLength === 2,
    "comprimento do prefixo dos shards deveria ser preservado");


var withFallback = [
    "#EXTM3U",
    "#EXT-X-LISTA-FALLBACK:https://raw.example/fallback",
    "#EXT-X-LISTA-FALLBACK-VERSION:v123",
    "#EXT-X-LISTA-FALLBACK-SHARD-LEN:2",
    "#EXTINF:-1 group-title=\"Filmes | Ação\" x-lista-fallback=\"abcdef0123456789abcd\",Filme F",
    "https://primary.example/f.mp4"
].join("\n");
var fallbackCatalog = providers.parseM3u(
    withFallback,
    "https://example.com/list.m3u8"
);
var fallbackItem = fallbackCatalog.vod.items[0];
assert(fallbackItem.url === "https://primary.example/f.mp4",
    "URL primária direta deveria ser preservada");
assert(fallbackItem.fallbackId === "abcdef0123456789abcd",
    "fallbackId deveria ser preservado");
assert(fallbackItem.fallbackIndexBase === "https://raw.example/fallback",
    "base estática de fallback deveria ser preservada");
assert(fallbackItem.fallbackIndexVersion === "v123",
    "versão do fallback deveria ser preservada");
assert(fallbackItem.fallbackIndexShardLength === 2,
    "comprimento do shard de fallback deveria ser preservado");


var streamedParser = providers.createM3uParser(
    "https://example.com/list.m3u8"
);
for (var offset = 0; offset < mixed.length; offset += 7) {
    streamedParser.consumeTextChunk(mixed.slice(offset, offset + 7));
}
var streamedCatalogs = streamedParser.finish();

assert(streamedParser.hasM3uMarker(),
    "parser incremental deveria reconhecer marcadores M3U");
assert(streamedCatalogs.series.items.length === catalogs.series.items.length,
    "parser incremental deveria preservar agrupamento de séries");
assert(streamedCatalogs.series.items[0].episodes.length ===
    catalogs.series.items[0].episodes.length,
    "parser incremental deveria preservar episódios");
assert(streamedCatalogs.live.items.length === catalogs.live.items.length,
    "parser incremental deveria preservar canais ao vivo");

providers.parseM3uAsync(mixed, "https://example.com/list.m3u8", {
    chunkChars: 32 * 1024
}).then(function (asyncCatalogs) {
    assert(asyncCatalogs.series.items.length === catalogs.series.items.length,
        "parser assíncrono deveria preservar séries");
    assert(asyncCatalogs.live.items.length === catalogs.live.items.length,
        "parser assíncrono deveria preservar canais");
    console.log("Tizen incremental provider tests: OK");
}).catch(function (error) {
    console.error(error);
    process.exitCode = 1;
});
