"use strict";

const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const source = fs.readFileSync(
  path.join(__dirname, "..", "app", "src", "storage.js"),
  "utf8"
);

const data = Object.create(null);
const localStorage = {
  getItem(key) {
    return Object.prototype.hasOwnProperty.call(data, key) ? data[key] : null;
  },
  setItem(key, value) {
    data[key] = String(value);
  },
  removeItem(key) {
    delete data[key];
  }
};

const sandbox = {
  window: { localStorage },
  Date
};

vm.runInNewContext(source, sandbox, { filename: "storage.js" });

const storage = sandbox.window.BlazzingStorage;
assert(storage);

const rawUrl = "https://provider.example/list.m3u8?token=secret";
const fp = storage.fingerprint(rawUrl);
assert.strictEqual(fp.length, 8);
assert(!fp.includes("secret"));

const key = "m3u:" + fp + ":" + storage.fingerprint("https://cdn.example/live.ts|News");
assert.strictEqual(storage.count(), 0);
assert.strictEqual(storage.isFavorite(key), false);

assert.strictEqual(storage.toggle(key, {
  title: "News",
  group: "TV",
  kind: "live",
  url: "https://must-not-be-stored.example/secret"
}), true);

assert.strictEqual(storage.isFavorite(key), true);
assert.strictEqual(storage.count(), 1);

const serialized = Object.values(data).join("\n");
assert(serialized.includes("News"));
assert(!serialized.includes("must-not-be-stored"));
assert(!serialized.includes(rawUrl));

assert.strictEqual(storage.toggle(key, {}), false);
assert.strictEqual(storage.count(), 0);

const mediaKey = "xtream:abcd1234:vod:42";
assert.strictEqual(storage.getProgress(mediaKey), 0);

storage.setProgress(mediaKey, 125.9, 3600);
assert.strictEqual(storage.getProgress(mediaKey), 125);

let progressSerialized = Object.values(data).join("\n");
assert(!progressSerialized.includes("http://"));
assert(!progressSerialized.includes("user"));
assert(!progressSerialized.includes("password"));

storage.setProgress(mediaKey, 3590, 3600);
assert.strictEqual(storage.getProgress(mediaKey), 0);

storage.setProgress(mediaKey, 300, 3600);
storage.clearProgress(mediaKey);
assert.strictEqual(storage.getProgress(mediaKey), 0);

console.log("Storage tests passed");
