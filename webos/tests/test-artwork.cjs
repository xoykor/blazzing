"use strict";

const assert = require("node:assert");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const artworkSource = fs.readFileSync(
  path.join(__dirname, "..", "app", "src", "artwork.js"),
  "utf8"
);
const serviceSource = fs.readFileSync(
  path.join(
    __dirname,
    "..",
    "service",
    "io.github.xoykor.blazzing.network",
    "network_service.js"
  ),
  "utf8"
);

const sandbox = {
  window: {
    URL: {
      createObjectURL() {
        return "blob:mock";
      },
      revokeObjectURL() {}
    }
  },
  Date,
  Promise
};

vm.runInNewContext(artworkSource, sandbox, {
  filename: "artwork.js"
});

const artwork = sandbox.window.BlazzingArtwork;
assert(artwork);
assert.strictEqual(typeof artwork.cacheKey, "function");
assert.strictEqual(artwork.limits.maxImageBytes, 1024 * 1024);
assert.strictEqual(artwork.limits.maxCacheBytes, 24 * 1024 * 1024);
assert.strictEqual(artwork.limits.maxCacheItems, 256);

const sensitiveUrl =
  "https://img.example/poster.jpg?token=super-secret&user=viewer";
const key = artwork.cacheKey(sensitiveUrl);
assert(key.startsWith("art:"));
assert(!key.includes("super-secret"));
assert(!key.includes("viewer"));
assert.strictEqual(key.length, 12);

assert(serviceSource.includes("MAX_ARTWORK_BYTES = 1024 * 1024"));
assert(serviceSource.includes('service.register("fetchArtwork"'));
assert(serviceSource.includes("Artwork exceeds the 1 MiB cache limit."));

(async () => {
  const direct = await artwork.load(sensitiveUrl);

  assert.strictEqual(direct.source, sensitiveUrl);
  assert.strictEqual(direct.objectUrl, false);
  assert.strictEqual(direct.cached, false);

  console.log("Artwork cache tests passed");
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
