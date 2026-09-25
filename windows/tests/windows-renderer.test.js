/* SPDX-License-Identifier: GPL-3.0-only */
"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const read = (name) => fs.readFileSync(path.join(root, name), "utf8");

const pkg = JSON.parse(read("package.json"));
const main = read("main.js");
const preload = read("preload.js");
const html = read("index.html");
const app = read(path.join("js", "app.js"));
const providers = read(path.join("js", "providers.js"));
const pairing = read(path.join("js", "pairing.js"));

assert.ok(!JSON.stringify(pkg).includes("tizen"), "Windows package must not depend on Tizen");
assert.ok(pkg.build.files.includes("index.html"));
assert.ok(main.includes('loadFile(path.join(__dirname, "index.html"))'));
assert.ok(!main.includes('app", "tizen"'));
assert.ok(main.includes('"net:request"'));
assert.ok(main.includes('["GET", "POST", "DELETE"]'));
assert.ok(preload.includes("netRequest"));
assert.ok(preload.includes("toggleFullscreen"));

assert.ok(html.includes('src="js/providers.js"'));
assert.ok(html.includes('src="js/pairing.js"'));
assert.ok(html.includes('src="js/app.js"'));
assert.ok(html.includes('id="pair-button"'));
assert.ok(html.includes('id="pairing-modal"'));
assert.ok(html.includes('data-kind="live"'));
assert.ok(html.includes('data-kind="vod"'));
assert.ok(html.includes('data-kind="series"'));

assert.ok(app.includes("providers.parseM3uAsync"));
assert.ok(app.includes("state.xtream.load(kind)"));
assert.ok(app.includes("loadStoredM3uCatalog(state.profile, kind)"));
assert.ok(app.includes("pairing.start(acceptPairedPlaylist"));
assert.ok(!app.includes('state.profile.mode !== "xtream" || kind === state.kind'));
assert.ok(!app.includes('button.getAttribute("data-kind") !== "live"'));

assert.ok(providers.includes("classifyGroup"));
assert.ok(providers.includes("parseEpisodeLabel"));
assert.ok(providers.includes("seriesSummaryOnly"));
assert.ok(pairing.includes("BlazzingWindowsNative"));
assert.ok(pairing.includes("native.netRequest"));

new Function(app);
new Function(providers);
new Function(pairing);

console.log("Windows renderer architecture checks passed.");
