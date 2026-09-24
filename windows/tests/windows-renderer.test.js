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

assert.strictEqual(pkg.version, "1.4.8");
assert.ok(!JSON.stringify(pkg).includes("tizen"), "Windows package must not depend on Tizen");
assert.ok(pkg.build.files.includes("index.html"));
assert.ok(main.includes('loadFile(path.join(__dirname, "index.html"))'));
assert.ok(!main.includes('app", "tizen"'));
assert.ok(preload.includes("toggleFullscreen"));
assert.ok(html.includes('src="js/app.js"'));
assert.ok(html.includes('id="back-series"'));
assert.ok(app.includes("get_live_streams"));
assert.ok(app.includes("get_vod_streams"));
assert.ok(app.includes("get_series"));
assert.ok(app.includes("parseM3u"));
assert.ok(app.includes("playerOpen"));

new Function(app);

console.log("Windows standalone renderer checks passed.");
