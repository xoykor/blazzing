/* SPDX-License-Identifier: GPL-3.0-only */
"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const windowsRoot = path.resolve(__dirname, "..");
const repoRoot = path.resolve(windowsRoot, "..");

function read(relative) {
    return fs.readFileSync(path.join(repoRoot, relative), "utf8");
}

const windowsCss = read("windows/css/app.css");
const windowsHtml = read("windows/index.html");
const tizenCss = read("tizen/css/app.css");
const linuxUi = read("src/ui_x11/x11_app.c");

const canonical = {
    background: "#090b11",
    text: "#f6f7fb",
    surface: "#171c28",
    muted: "#aab2c4",
    accent: "#ff5f2e",
    accentWarm: "#ff8a45"
};

Object.values(canonical).forEach(function (token) {
    assert.ok(
        tizenCss.toLowerCase().includes(token),
        "Tizen must define canonical token " + token
    );
    assert.ok(
        windowsCss.toLowerCase().includes(token),
        "Windows must define canonical token " + token
    );
});

assert.ok(linuxUi.includes("#090B11"), "Linux must use canonical background");
assert.ok(linuxUi.includes("#F6F7FB"), "Linux must use canonical text");
assert.ok(linuxUi.includes("#AAB2C4"), "Linux must use canonical muted text");
assert.ok(linuxUi.includes("#FF5F2E"), "Linux must use canonical orange accent");
assert.ok(linuxUi.includes("0xFF8A45u"), "Linux must use canonical warm accent");

assert.ok(!windowsCss.toLowerCase().includes("#62a9ff"), "Windows must not regress to the old blue accent");
assert.ok(!linuxUi.includes("#62A9FF"), "Linux must not regress to the old blue accent");
assert.ok(!linuxUi.includes("0x62A9FFu"), "Linux renderer must not regress to the old blue accent");

assert.ok(windowsHtml.includes('class="hero"'), "Windows home must keep the Tizen hero hierarchy");
assert.ok(windowsHtml.includes('class="topnav"'), "Windows catalog must keep the Tizen top navigation hierarchy");
assert.ok(windowsHtml.includes('class="catalog-layout"'), "Windows catalog must keep the Tizen sidebar/content hierarchy");
assert.ok(windowsCss.includes("box-shadow:0 0 0 4px var(--accent)"), "Windows cards must keep orange focus ring");
assert.ok(windowsCss.includes("border-color:var(--focus)"), "Windows cards must keep white focus edge");

console.log("Cross-port visual identity checks passed.");
