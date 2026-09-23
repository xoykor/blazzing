/* SPDX-License-Identifier: GPL-3.0-only */
"use strict";

const fs = require("fs");
const path = require("path");

const windowsRoot = path.resolve(__dirname, "..");
const source = path.resolve(windowsRoot, "..", "tizen");
const appRoot = path.join(windowsRoot, "app");
const destination = path.join(appRoot, "tizen");

fs.rmSync(appRoot, { recursive: true, force: true });
fs.mkdirSync(appRoot, { recursive: true });
fs.cpSync(source, destination, { recursive: true });

const indexPath = path.join(destination, "index.html");
let html = fs.readFileSync(indexPath, "utf8");

html = html
    .replace(/\s*<script src="\$WEBAPIS\/webapis\/webapis\.js"><\/script>/, "")
    .replace(/<object id="av-player-object"[\s\S]*?<\/object>/, "")
    .replace("IPTV para Samsung Tizen", "IPTV para Windows")
    .replace("Tizen Web App", "Windows Desktop")
    .replace(
        '<script src="js/player.js"></script>',
        '<script src="js/player.js"></script>\n    <script src="../../js/platform.js"></script>'
    );

fs.writeFileSync(indexPath, html, "utf8");
console.log("Windows web assets prepared from tizen/.");
