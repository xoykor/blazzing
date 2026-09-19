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
  !navigationSource.includes(
    "function dispatchBoundary(dx, dy) {\n    dispatchBoundary(dx, dy);"
  ),
  "navigation boundary dispatcher must not recurse into itself"
);
assert(
  navigationSource.includes("focusableAncestor"),
  "pointer hover must focus nested content through its focusable ancestor"
);
assert(
  navigationSource.includes("data-has-prev-window") &&
  navigationSource.includes("data-has-next-window"),
  "remote navigation must honor explicit catalog window boundaries"
);
assert(
  appSource.includes("catalogFocusRequest"),
  "catalog must preserve focus across paged navigation"
);
assert(
  appSource.includes("updateCatalogWindowBoundary"),
  "catalog renderer must publish previous/next window availability"
);

const cssSource = fs.readFileSync(
  path.join(__dirname, "..", "app", "css", "app.css"),
  "utf8"
);
assert(
  cssSource.includes("@media (max-width: 1366px)") &&
  cssSource.includes("width: calc(33.333% - 12px)"),
  "720p layout must keep the home actions in a compact three-column grid"
);

const appInfo = JSON.parse(fs.readFileSync(
  path.join(__dirname, "..", "app", "appinfo.json"),
  "utf8"
));
assert.strictEqual(
  appInfo.disableBackHistoryAPI,
  true,
  "manual Back handling requires disableBackHistoryAPI"
);
assert(
  appSource.includes("webOS.platformBack"),
  "Home Back must delegate to webOS.platformBack"
);
assert(
  html.includes('id="player-toggle"') &&
  appSource.includes('byId("player-toggle").addEventListener("click"'),
  "player must expose a remote- and pointer-operable Play/Pause control"
);
const playerSource = fs.readFileSync(
  path.join(__dirname, "..", "app", "src", "player.js"),
  "utf8"
);
assert(
  playerSource.includes("element.onpause") &&
  playerSource.includes("isPaused: isPaused"),
  "player must report paused state to the on-screen control"
);

console.log("UI contract tests passed");
