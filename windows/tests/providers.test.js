/* SPDX-License-Identifier: GPL-3.0-only */
"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const source = fs.readFileSync(
    path.resolve(__dirname, "..", "js", "providers.js"),
    "utf8"
);

const context = {
    window: {},
    document: {
        createElement: function () {
            return { protocol: "", host: "", href: "" };
        }
    },
    console: console,
    Promise: Promise,
    setTimeout: function (fn) { fn(); }
};

vm.createContext(context);
vm.runInContext(source, context);

const providers = context.window.BlazzingProviders;
assert.ok(providers);

const playlist = [
    "#EXTM3U",
    "#EXT-X-LISTA-CARDS:https://cards.example/catalog",
    "#EXT-X-LISTA-CARDS-VERSION:v-test",
    "#EXT-X-LISTA-CARDS-SHARD-LEN:2",
    '#EXTINF:-1 group-title="CANAIS | Aberta" tvg-logo="https://img/tv.png",Canal Teste',
    "https://media/live.ts",
    '#EXTINF:-1 group-title="FILMES | Ação" tvg-logo="https://img/movie.png",Filme Teste',
    "https://media/movie.mp4",
    '#EXTINF:-1 group-title="Séries | Drama" tvg-logo="https://img/show.png",Minha Série S01E02',
    "https://media/show-s01e02.mp4",
    '#EXTINF:-1 group-title="Séries | Drama" tvg-logo="https://img/show.png",Minha Série S01E01',
    "https://media/show-s01e01.mp4"
].join("\n");

(async function () {
    const catalogs = providers.parseM3u(playlist, "https://example/list.m3u");

    assert.strictEqual(catalogs.live.items.length, 1);
    assert.strictEqual(catalogs.vod.items.length, 1);
    assert.strictEqual(catalogs.series.items.length, 1);
    assert.strictEqual(catalogs.series.items[0].episodeCount, 2);
    assert.strictEqual(catalogs.series.items[0].episodes.length, 2);
    assert.strictEqual(catalogs.series.items[0].episodes[0].episode, 1);
    assert.strictEqual(catalogs.series.items[0].episodes[1].episode, 2);

    // Keep Lista artwork fallback keys even when tvg-logo is present.
    assert.ok(catalogs.live.items[0].cardKey);
    assert.ok(catalogs.vod.items[0].cardKey);
    assert.ok(catalogs.series.items[0].cardKey);
    assert.strictEqual(
        catalogs.vod.items[0].cardIndexBase,
        "https://cards.example/catalog"
    );
    assert.strictEqual(catalogs.vod.items[0].cardIndexVersion, "v-test");
    assert.strictEqual(catalogs.vod.items[0].cardIndexShardLength, 2);

    const liveOnly = await providers.parseM3uAsync(
        playlist,
        "https://example/list.m3u",
        { onlyKind: "live" }
    );
    const vodOnly = await providers.parseM3uAsync(
        playlist,
        "https://example/list.m3u",
        { onlyKind: "vod" }
    );
    const seriesOnly = await providers.parseM3uAsync(
        playlist,
        "https://example/list.m3u",
        { onlyKind: "series", seriesSummaryOnly: true }
    );

    assert.strictEqual(liveOnly.live.items.length, 1);
    assert.strictEqual(liveOnly.vod.items.length, 0);
    assert.strictEqual(vodOnly.vod.items.length, 1);
    assert.strictEqual(vodOnly.live.items.length, 0);
    assert.strictEqual(seriesOnly.series.items.length, 1);
    assert.strictEqual(seriesOnly.series.items[0].episodeCount, 2);
    assert.strictEqual(seriesOnly.series.items[0].episodes, null);

    console.log("Windows provider classification checks passed.");
}()).catch(function (error) {
    console.error(error);
    process.exitCode = 1;
});
