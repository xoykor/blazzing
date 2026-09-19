"use strict";

const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const uiSource = fs.readFileSync(
  path.join(__dirname, "..", "app", "src", "pluto.js"),
  "utf8"
);
const service = require(
  path.join(
    __dirname,
    "..",
    "service",
    "io.github.xoykor.blazzing.network",
    "pluto_service.js"
  )
);

const sandbox = { window: {} };
vm.runInNewContext(uiSource, sandbox, { filename: "pluto.js" });

const pluto = sandbox.window.BlazzingPluto;
assert(pluto);

const guide = pluto.buildLiveCatalog({
  mode: "guide-v2",
  categories: [
    {
      name: "Filmes",
      channelIDs: ["abc123"]
    }
  ],
  channels: [
    {
      id: "abc123",
      name: "Pluto Filmes",
      number: 100,
      images: [
        {
          type: "colorLogoPNG",
          url: "https://img.example/pluto.png"
        }
      ]
    }
  ]
});

assert.strictEqual(guide.items.length, 1);
assert.strictEqual(guide.items[0].title, "Pluto Filmes");
assert.strictEqual(guide.items[0].group, "Filmes");
assert.strictEqual(guide.items[0].channelNumber, "100");
assert.strictEqual(guide.items[0].kind, "pluto-live");
assert.strictEqual(guide.items[0].plutoChannelId, "abc123");
assert.strictEqual(guide.items[0].url, "");
assert.strictEqual(guide.items[0].favoriteKey, "pluto:live:abc123");
assert.strictEqual(
  guide.items[0].logo,
  "https://img.example/pluto.png"
);

const legacy = pluto.buildLiveCatalog({
  mode: "legacy",
  channels: [
    {
      _id: "legacy456",
      name: "Pluto Retrô",
      number: "205",
      category: "Retrô",
      colorLogoPNG: {
        path: "https://img.example/retro.png"
      }
    }
  ]
});

assert.strictEqual(legacy.items.length, 1);
assert.strictEqual(legacy.items[0].group, "Retrô");
assert.strictEqual(legacy.items[0].logo, "https://img.example/retro.png");

const session = {
  sessionToken: "header.payload.signature",
  stitcher: "https://stitcher.example/",
  stitcherParams: "deviceType=web&deviceMake=chrome"
};

const streamUrl = service.buildStreamUrl(session, "abc123");
assert(streamUrl.startsWith(
  "https://stitcher.example/v2/stitch/hls/channel/abc123/master.m3u8?"
));
assert(streamUrl.includes("jwt=header.payload.signature"));
assert(streamUrl.includes("masterJWTPassthrough=true"));
assert(streamUrl.includes("includeExtendedEvents=true"));
assert(streamUrl.includes("deviceType=web"));
assert.strictEqual(service.validChannelId("abc123"), true);
assert.strictEqual(service.validChannelId("../bad"), false);

const serializedCatalog = JSON.stringify(guide);
assert(!serializedCatalog.includes("jwt="));
assert(!serializedCatalog.includes("sessionToken"));

console.log("Pluto TV tests passed");
