"use strict";

const assert = require("node:assert");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const helper = require(
  path.join(
    __dirname,
    "..",
    "service",
    "io.github.xoykor.blazzing.network",
    "m3u_catalog.js"
  )
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

assert(
  serviceSource.includes("128 * 1024 * 1024"),
  "network service must enforce a 128 MiB M3U limit"
);
assert(serviceSource.includes('service.register("prepareM3U"'));
assert(serviceSource.includes('service.register("queryM3U"'));
assert(serviceSource.includes('service.register("releaseM3U"'));

const lines = ["#EXTM3U"];
for (let i = 0; i < 130; i += 1) {
  const group = i % 2 === 0 ? "Even" : "Odd";
  lines.push(
    '#EXTINF:-1 group-title="' + group + '" tvg-logo="https://img.example/' +
      i + '.png",Channel ' + i
  );
  lines.push("streams/" + i + ".m3u8");
}

const playlist = lines.join("\n") + "\n";
const buffer = Buffer.from(playlist, "utf8");
const scanner = helper.createMetadataScanner(
  "https://provider.example/catalog/main.m3u"
);

for (let offset = 0; offset < buffer.length; offset += 17) {
  scanner.feed(buffer.slice(offset, Math.min(buffer.length, offset + 17)));
}

const metadata = scanner.finish();
assert.strictEqual(metadata.itemCount, 130);
assert.deepStrictEqual(metadata.groups, ["Even", "Odd"]);

const tempFile = path.join(
  os.tmpdir(),
  "blazzing-large-m3u-test-" + process.pid + ".m3u"
);
fs.writeFileSync(tempFile, buffer);

function query(options) {
  return new Promise((resolve, reject) => {
    helper.queryPage(
      Object.assign({
        filePath: tempFile,
        fileSize: buffer.length,
        baseUrl: "https://provider.example/catalog/main.m3u",
        startOffset: 0,
        limit: 48,
        group: "",
        query: "",
        favoritesOnly: false,
        favoriteIds: []
      }, options || {}),
      (error, result) => {
        if (error) {
          reject(error);
          return;
        }
        resolve(result);
      }
    );
  });
}

(async () => {
  try {
    const first = await query();
    assert.strictEqual(first.items.length, 48);
    assert.strictEqual(first.items[0].title, "Channel 0");
    assert.strictEqual(first.items[47].title, "Channel 47");
    assert.strictEqual(first.hasMore, true);
    assert(first.nextOffset > 0);

    const second = await query({ startOffset: first.nextOffset });
    assert.strictEqual(second.items.length, 48);
    assert.strictEqual(second.items[0].title, "Channel 48");
    assert.strictEqual(second.items[47].title, "Channel 95");
    assert.strictEqual(second.hasMore, true);

    const groupPage = await query({ group: "Odd", limit: 10 });
    assert.strictEqual(groupPage.items.length, 10);
    assert(groupPage.items.every((item) => item.group === "Odd"));

    const search = await query({ query: "channel 12" });
    assert(search.items.length >= 1);
    assert(search.items.every((item) =>
      item.title.toLowerCase().includes("channel 12")
    ));

    const favoriteItem = first.items[3];
    const favoriteId = helper.stableHash(
      favoriteItem.url + "|" + favoriteItem.title
    );
    const favorites = await query({
      favoritesOnly: true,
      favoriteIds: [favoriteId]
    });
    assert.strictEqual(favorites.items.length, 1);
    assert.strictEqual(favorites.items[0].title, favoriteItem.title);

    console.log("Large M3U paging tests passed");
  } finally {
    try {
      fs.unlinkSync(tempFile);
    } catch (error) {
      // Best effort.
    }
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
