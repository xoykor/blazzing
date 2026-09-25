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
    '#EXTINF:-1 group-title="CANAIS | Aberta" tvg-logo="https://img/tv.png",Canal Teste',
    "https://media/live.ts",
    '#EXTINF:-1 group-title="FILMES | Ação" tvg-logo="https://img/movie.png",Filme Teste',
    "https://media/movie.mp4",
    '#EXTINF:-1 group-title="Séries | Drama" tvg-logo="https://img/show.png",Minha Série S01E02',
    "https://media/show-s01e02.mp4",
    '#EXTINF:-1 group-title="Séries | Drama" tvg-logo="https://img/show.png",Minha Série S01E01',
    "https://media/show-s01e01.mp4"
].join("\n");

const catalogs = providers.parseM3u(playlist, "https://example/list.m3u");

assert.strictEqual(catalogs.live.items.length, 1);
assert.strictEqual(catalogs.vod.items.length, 1);
assert.strictEqual(catalogs.series.items.length, 1);
assert.strictEqual(catalogs.series.items[0].episodeCount, 2);
assert.strictEqual(catalogs.series.items[0].episodes.length, 2);
assert.strictEqual(catalogs.series.items[0].episodes[0].episode, 1);
assert.strictEqual(catalogs.series.items[0].episodes[1].episode, 2);

assert.strictEqual(catalogs.live.items[0].kind, "live");
assert.strictEqual(catalogs.vod.items[0].kind, "vod");
assert.strictEqual(catalogs.series.items[0].kind, "series");

console.log("Windows provider classification checks passed.");
