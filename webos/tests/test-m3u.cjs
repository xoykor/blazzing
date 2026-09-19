"use strict";

const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(
  path.join(__dirname, "..", "app", "src", "m3u.js"),
  "utf8"
);

const sandbox = {
  window: {},
  URL: URL
};

vm.runInNewContext(source, sandbox, { filename: "m3u.js" });

const parser = sandbox.window.BlazzingM3U;
assert(parser && typeof parser.parse === "function");

const playlist = [
  "#EXTM3U",
  '#EXTINF:-1 tvg-name="News One" group-title="News" tvg-logo="https://img/news.png",News One',
  "https://media.example/news.m3u8",
  '#EXTINF:-1 group-title="Sports",Sports One',
  "streams/sports.m3u8",
  "#EXTINF:-1,No Group",
  "https://media.example/misc.m3u8"
].join("\n");

const result = parser.parse(playlist, "https://provider.example/list/main.m3u");

assert.strictEqual(result.items.length, 3);
assert.deepStrictEqual(Array.from(result.groups), ["News", "Sem categoria", "Sports"]);
assert.strictEqual(result.items[0].title, "News One");
assert.strictEqual(result.items[0].group, "News");
assert.strictEqual(result.items[0].logo, "https://img/news.png");
assert.strictEqual(result.items[1].url, "https://provider.example/list/streams/sports.m3u8");
assert.strictEqual(result.items[2].group, "Sem categoria");

const bare = parser.parse("https://example.org/live.m3u8", "");
assert.strictEqual(bare.items.length, 1);
assert.strictEqual(bare.items[0].title, "https://example.org/live.m3u8");

console.log("M3U parser tests passed");
