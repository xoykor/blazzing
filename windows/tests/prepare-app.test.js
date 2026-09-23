/* SPDX-License-Identifier: GPL-3.0-only */
"use strict";

const fs = require("fs");
const path = require("path");

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

const root = path.resolve(__dirname, "..");
const indexPath = path.join(root, "app", "tizen", "index.html");
const html = fs.readFileSync(indexPath, "utf8");

assert(html.includes("IPTV para Windows"),
    "prepared frontend should identify the Windows port");
assert(html.includes('../../js/platform.js'),
    "prepared frontend should load the Windows bridge");
assert(!html.includes("$WEBAPIS/webapis/webapis.js"),
    "prepared frontend must not load Samsung WebAPIs");
assert(!html.includes('id="av-player-object"'),
    "prepared frontend must not retain the Samsung AVPlay object");
assert(fs.existsSync(path.join(root, "app", "LICENSE")),
    "packaged frontend should include the GPL license");
assert(fs.existsSync(path.join(root, "app", "THIRD_PARTY_NOTICES.md")),
    "packaged frontend should include third-party notices");

console.log("Windows prepare-app tests: OK");
