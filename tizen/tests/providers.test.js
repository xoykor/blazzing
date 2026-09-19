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
