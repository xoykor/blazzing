"use strict";

var fs = require("fs");
var urlModule = require("url");

function emptyBuffer() {
  return Buffer.alloc ? Buffer.alloc(0) : new Buffer(0);
}

function parseAttributes(line) {
  var attrs = {};
  var expression = /([A-Za-z0-9_-]+)="([^"]*)"/g;
  var match;

  while ((match = expression.exec(line)) !== null) {
    attrs[match[1]] = match[2];
  }

  return attrs;
}

function stableHash(value) {
  var text = String(value || "");
  var hash = 2166136261;
  var i;

  for (i = 0; i < text.length; i += 1) {
    hash ^= text.charCodeAt(i);
    hash += (hash << 1) + (hash << 4) + (hash << 7) +
      (hash << 8) + (hash << 24);
  }

  return ("00000000" + (hash >>> 0).toString(16)).slice(-8);
}

function validHttpUrl(value) {
  return /^https?:\/\//i.test(String(value || ""));
}

function resolveUrl(value, baseUrl) {
  if (validHttpUrl(value)) {
    return value;
  }

  if (!baseUrl) {
    return value;
  }

  try {
    return urlModule.resolve(baseUrl, value);
  } catch (error) {
    return value;
  }
}

function createLineParser(baseUrl, onItem) {
  var pending = null;

  function consume(rawLine, lineEndOffset) {
    var line = String(rawLine || "").replace(/\r$/, "").trim();
    var attrs;
    var comma;
    var title;
    var group;
    var item;

    if (!line) {
      return true;
    }

    if (line.indexOf("#EXTINF:") === 0) {
      attrs = parseAttributes(line);
      comma = line.lastIndexOf(",");
      title = comma >= 0 ? line.slice(comma + 1).trim() : "";
      pending = {
        title: title || attrs["tvg-name"] || "Sem título",
        group: attrs["group-title"] || "Sem categoria",
        logo: attrs["tvg-logo"] || ""
      };
      return true;
    }

    if (line.charAt(0) === "#") {
      return true;
    }

    if (!pending) {
      pending = {
        title: line,
        group: "Sem categoria",
        logo: ""
      };
    }

    group = pending.group || "Sem categoria";
    item = {
      title: pending.title,
      group: group,
      logo: pending.logo,
      url: resolveUrl(line, baseUrl)
    };
    pending = null;

    if (!validHttpUrl(item.url)) {
      return true;
    }

    return onItem(item, lineEndOffset) !== false;
  }

  return {
    consume: consume
  };
}

function eachBufferLine(state, chunk, onLine) {
  var combined;
  var start;
  var i;
  var line;
  var lineEnd;

  if (!chunk || !chunk.length) {
    return true;
  }

  combined = state.carry.length ?
    Buffer.concat([state.carry, chunk]) :
    chunk;
  start = 0;

  for (i = 0; i < combined.length; i += 1) {
    if (combined[i] !== 10) {
      continue;
    }

    line = combined.slice(start, i).toString("utf8");
    lineEnd = state.carryOffset + i + 1;

    if (onLine(line, lineEnd) === false) {
      return false;
    }

    start = i + 1;
  }

  state.carry = combined.slice(start);
  state.carryOffset += start;
  return true;
}

function createMetadataScanner(baseUrl) {
  var groups = [];
  var seenGroups = {};
  var itemCount = 0;
  var state = {
    carry: emptyBuffer(),
    carryOffset: 0
  };
  var parser = createLineParser(baseUrl, function (item) {
    itemCount += 1;
    if (!seenGroups[item.group]) {
      seenGroups[item.group] = true;
      groups.push(item.group);
    }
    return true;
  });

  function feed(chunk) {
    eachBufferLine(state, chunk, function (line, lineEnd) {
      return parser.consume(line, lineEnd);
    });
  }

  function finish() {
    if (state.carry.length) {
      parser.consume(
        state.carry.toString("utf8"),
        state.carryOffset + state.carry.length
      );
      state.carry = emptyBuffer();
    }

    groups.sort(function (a, b) {
      return a.toLowerCase().localeCompare(b.toLowerCase());
    });

    return {
      groups: groups,
      itemCount: itemCount
    };
  }

  return {
    feed: feed,
    finish: finish
  };
}

function scanFileLines(filePath, startOffset, onLine, callback) {
  var done = false;
  var state = {
    carry: emptyBuffer(),
    carryOffset: startOffset
  };
  var stream = fs.createReadStream(filePath, {
    start: startOffset,
    highWaterMark: 256 * 1024
  });

  function finish(error) {
    if (done) {
      return;
    }
    done = true;
    callback(error || null);
  }

  stream.on("data", function (chunk) {
    var keepGoing = eachBufferLine(state, chunk, onLine);

    if (!keepGoing) {
      stream.destroy();
      finish(null);
    }
  });

  stream.on("end", function () {
    if (done) {
      return;
    }

    if (state.carry.length) {
      if (onLine(
        state.carry.toString("utf8"),
        state.carryOffset + state.carry.length
      ) === false) {
        finish(null);
        return;
      }
    }

    finish(null);
  });

  stream.on("error", function (error) {
    finish(error);
  });
}

function queryPage(options, callback) {
  var limit = Math.max(1, Math.min(48, Number(options.limit || 48)));
  var startOffset = Math.max(0, Number(options.startOffset || 0));
  var group = String(options.group || "");
  var query = String(options.query || "").toLowerCase();
  var favoritesOnly = options.favoritesOnly === true;
  var favoriteSet = {};
  var favoriteIds = Array.isArray(options.favoriteIds) ?
    options.favoriteIds.slice(0, 5000) : [];
  var items = [];
  var hasMore = false;
  var pageEndOffset = startOffset;
  var parser;
  var i;

  for (i = 0; i < favoriteIds.length; i += 1) {
    favoriteSet[String(favoriteIds[i])] = true;
  }

  if (startOffset >= Number(options.fileSize || 0)) {
    callback(null, {
      items: [],
      hasMore: false,
      nextOffset: Number(options.fileSize || 0)
    });
    return;
  }

  parser = createLineParser(options.baseUrl, function (item, lineEndOffset) {
    var itemId;
    var haystack;

    if (group && item.group !== group) {
      return true;
    }

    if (query) {
      haystack = (item.title + " " + item.group).toLowerCase();
      if (haystack.indexOf(query) < 0) {
        return true;
      }
    }

    itemId = stableHash(item.url + "|" + item.title);

    if (favoritesOnly && !favoriteSet[itemId]) {
      return true;
    }

    if (items.length < limit) {
      item.itemId = itemId;
      item.kind = "m3u";
      items.push(item);

      if (items.length === limit) {
        pageEndOffset = lineEndOffset;
      }
      return true;
    }

    hasMore = true;
    return false;
  });

  scanFileLines(
    options.filePath,
    startOffset,
    function (line, lineEndOffset) {
      return parser.consume(line, lineEndOffset);
    },
    function (error) {
      if (error) {
        callback(error);
        return;
      }

      callback(null, {
        items: items,
        hasMore: hasMore,
        nextOffset: hasMore ?
          pageEndOffset :
          Number(options.fileSize || pageEndOffset)
      });
    }
  );
}

module.exports = {
  createMetadataScanner: createMetadataScanner,
  queryPage: queryPage,
  stableHash: stableHash
};
