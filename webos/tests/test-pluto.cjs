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

const vodCatalog = pluto.buildVodCatalog({
  categories: [
    {
      name: "Destaques",
      items: [
        {
          _id: "movie123",
          name: "Pluto Movie",
          type: "movie",
          genre: "Ação",
          summary: "A test movie.",
          duration: 5400000,
          rating: "12",
          originalReleaseDate: "2024-06-01",
          covers: [
            { url: "https://img.example/movie.jpg" }
          ],
          stitched: {
            urls: [
              {
                type: "hls",
                url: "https://old.example/stitch/hls/episode/movie123/master.m3u8?jwt=STALE"
              }
            ]
          }
        },
        {
          _id: "series123",
          name: "Pluto Series",
          type: "series",
          genre: "Drama",
          seasonsNumbers: [1],
          covers: [
            { url: "https://img.example/series.jpg" }
          ]
        }
      ]
    },
    {
      name: "Filmes",
      items: [
        {
          _id: "movie123",
          name: "Pluto Movie",
          type: "movie",
          stitched: {
            urls: [
              {
                url: "https://old.example/stitch/hls/episode/movie123/master.m3u8?jwt=OTHER"
              }
            ]
          }
        }
      ]
    }
  ]
});

assert.strictEqual(vodCatalog.items.length, 2);
assert.strictEqual(vodCatalog.items[0].kind, "pluto-movie");
assert.strictEqual(vodCatalog.items[0].plutoContentId, "movie123");
assert.strictEqual(
  vodCatalog.items[0].plutoVodPath,
  "/stitch/hls/episode/movie123/master.m3u8"
);
assert.strictEqual(vodCatalog.items[0].duration, "1h 30min");
assert.strictEqual(vodCatalog.items[0].year, "2024");
assert.strictEqual(vodCatalog.items[1].kind, "pluto-series");
assert.strictEqual(vodCatalog.items[1].plutoSeriesId, "series123");

const vodSerialized = JSON.stringify(vodCatalog);
assert(!vodSerialized.includes("STALE"));
assert(!vodSerialized.includes("jwt="));
assert(!vodSerialized.includes("old.example"));

const episodes = pluto.buildEpisodeCatalog({
  name: "Pluto Series",
  genre: "Drama",
  seasons: [
    {
      number: 1,
      episodes: [
        {
          _id: "episode123",
          name: "Pilot",
          number: 1,
          duration: 1800000,
          stitched: {
            urls: [
              {
                url: "https://old.example/stitch/hls/episode/episode123/master.m3u8?jwt=OLD"
              }
            ]
          }
        }
      ]
    }
  ]
}, vodCatalog.items[1]);

assert.strictEqual(episodes.items.length, 1);
assert.strictEqual(episodes.items[0].kind, "pluto-episode");
assert.strictEqual(episodes.items[0].group, "Temporada 1");
assert.strictEqual(episodes.items[0].duration, "30min");
assert.strictEqual(
  episodes.items[0].plutoVodPath,
  "/stitch/hls/episode/episode123/master.m3u8"
);
assert(!JSON.stringify(episodes).includes("jwt="));

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
assert.strictEqual(
  service.sanitizeVodPath(
    "https://old.example/stitch/hls/episode/movie123/master.m3u8?jwt=STALE"
  ),
  "/stitch/hls/episode/movie123/master.m3u8"
);
assert.strictEqual(
  service.sanitizeVodPath("https://old.example/not-stitch/movie.m3u8"),
  ""
);

const vodStreamUrl = service.buildVodStreamUrl(
  session,
  "/stitch/hls/episode/movie123/master.m3u8"
);
assert(vodStreamUrl.startsWith(
  "https://stitcher.example/v2/stitch/hls/episode/movie123/master.m3u8?"
));
assert(vodStreamUrl.includes("jwt=header.payload.signature"));
assert(vodStreamUrl.includes("masterJWTPassthrough=true"));

const serializedCatalog = JSON.stringify(guide);
assert(!serializedCatalog.includes("jwt="));
assert(!serializedCatalog.includes("sessionToken"));

console.log("Pluto TV tests passed");
