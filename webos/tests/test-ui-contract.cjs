"use strict";

const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");

const appSource = fs.readFileSync(
  path.join(__dirname, "..", "app", "src", "app.js"),
  "utf8"
);
const html = fs.readFileSync(
  path.join(__dirname, "..", "app", "index.html"),
  "utf8"
);

const ids = new Set();
let match;
const byIdExpression = /byId\("([^"]+)"\)/g;

while ((match = byIdExpression.exec(appSource)) !== null) {
  ids.add(match[1]);
}

assert(ids.size > 0, "expected literal byId references in app.js");

for (const id of ids) {
  assert(
    html.includes('id="' + id + '"'),
    "app.js references missing DOM id: " + id
  );
}

const requiredScreens = [
  "home",
  "pair",
  "m3u",
  "catalog",
  "xtream",
  "pluto",
  "manual",
  "about",
  "vod",
  "series",
  "player"
];

for (const screen of requiredScreens) {
  assert(
    html.includes('id="screen-' + screen + '"'),
    "missing screen: " + screen
  );
}

const navigationSource = fs.readFileSync(
  path.join(__dirname, "..", "app", "src", "navigation.js"),
  "utf8"
);

assert(
  navigationSource.includes("blazzing-navigation-boundary"),
  "navigation must emit catalog boundary events"
);
assert(
  appSource.includes("catalogFocusRequest"),
  "catalog must preserve focus across paged navigation"
);

console.log("UI contract tests passed");
