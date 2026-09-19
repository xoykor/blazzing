"use strict";

const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(
  path.join(__dirname, "..", "app", "src", "xtream.js"),
  "utf8"
);

const sandbox = { window: {} };
vm.runInNewContext(source, sandbox, { filename: "xtream.js" });

const xtream = sandbox.window.BlazzingXtream;
assert(xtream);

const creds = xtream.credentials("http://provider.example:8080", "user name", "p@ss");
assert.strictEqual(creds.server, "http://provider.example:8080/");
assert(
  xtream.apiUrl(creds, "get_live_streams").includes("username=user%20name")
);

assert.strictEqual(
  xtream.authAccepted({ user_info: { auth: 1, status: "Active" } }),
  true
);
assert.strictEqual(
  xtream.authAccepted({ user_info: { auth: 0, status: "Disabled" } }),
  false
);

const catalog = xtream.buildLiveCatalog(
  [
    { category_id: "10", category_name: "News" },
    { category_id: "20", category_name: "Sports" }
  ],
  [
    { stream_id: 1, name: "News One", stream_type: "live", category_id: "10" },
    { stream_id: "2", name: "Sports One", stream_type: "live", category_id: "20",
      direct_source: "https://cdn.example/direct.m3u8" },
    { stream_id: 3, name: "Movie", stream_type: "movie", category_id: "20" }
  ],
  creds
);

assert.strictEqual(catalog.items.length, 2);
assert.deepStrictEqual(Array.from(catalog.groups), ["News", "Sports"]);
assert.strictEqual(catalog.items[0].group, "News");
assert.strictEqual(
  catalog.items[0].url,
  "http://provider.example:8080/live/user%20name/p%40ss/1.ts"
);
assert.strictEqual(catalog.items[1].url, "https://cdn.example/direct.m3u8");

const vod = xtream.buildVodCatalog(
  [
    { category_id: "30", category_name: "Action" }
  ],
  [
    {
      stream_id: 9,
      name: "Movie One",
      category_id: "30",
      container_extension: "mkv"
    },
    {
      stream_id: 10,
      title: "Direct Movie",
      category_id: "30",
      direct_source: "https://cdn.example/movie.mp4"
    }
  ],
  creds
);

assert.strictEqual(vod.items.length, 2);
assert.deepStrictEqual(Array.from(vod.groups), ["Action"]);
assert.strictEqual(vod.items[0].kind, "vod");
assert.strictEqual(
  vod.items[0].url,
  "http://provider.example:8080/movie/user%20name/p%40ss/9.mkv"
);
assert.strictEqual(vod.items[1].url, "https://cdn.example/movie.mp4");

console.log("Xtream parser tests passed");
