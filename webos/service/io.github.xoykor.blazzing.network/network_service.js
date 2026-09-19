"use strict";

var Service = require("webos-service");
var http = require("http");
var https = require("https");
var urlModule = require("url");
var fs = require("fs");
var pathModule = require("path");
var os = require("os");
var m3uCatalog = require("./m3u_catalog");
var plutoService = require("./pluto_service");

var service = new Service("io.github.xoykor.blazzing.network");
var MAX_TEXT_BYTES = 8 * 1024 * 1024;
var MAX_M3U_BYTES = 128 * 1024 * 1024;
var MAX_ARTWORK_BYTES = 1024 * 1024;
var MAX_REDIRECTS = 5;
var TIMEOUT_MS = 15000;
var M3U_SESSION_TTL_MS = 12 * 60 * 60 * 1000;
var M3U_FILE_PREFIX = "blazzing-webos-m3u-";
var m3uSessions = {};
var XTREAM_ACTIONS = {
  "": true,
  "get_live_categories": true,
  "get_live_streams": true,
  "get_vod_categories": true,
  "get_vod_streams": true,
  "get_vod_info": true,
  "get_series_categories": true,
  "get_series": true,
  "get_series_info": true
};

function validUrl(value) {
  return typeof value === "string" && /^https?:\/\//i.test(value);
}

function normalizeServer(value) {
  var server = String(value || "");
  if (!validUrl(server)) {
    return null;
  }
  return server.replace(/\/+$/, "") + "/";
}

function makeSessionId() {
  return Date.now().toString(36) + "-" +
    Math.floor(Math.random() * 0x7fffffff).toString(36) + "-" +
    Math.floor(Math.random() * 0x7fffffff).toString(36);
}

function safeUnlink(filePath) {
  if (!filePath) {
    return;
  }

  fs.unlink(filePath, function () {
    // Temporary-file cleanup is best effort.
  });
}

function cleanupSessions() {
  var now = Date.now();
  var ids = Object.keys(m3uSessions);
  var i;
  var session;

  for (i = 0; i < ids.length; i += 1) {
    session = m3uSessions[ids[i]];
    if (!session || now - session.touchedAt > M3U_SESSION_TTL_MS) {
      if (session) {
        safeUnlink(session.filePath);
      }
      delete m3uSessions[ids[i]];
    }
  }
}

function cleanupOrphanedFiles() {
  var dir = os.tmpdir();

  fs.readdir(dir, function (error, names) {
    var i;

    if (error || !Array.isArray(names)) {
      return;
    }

    for (i = 0; i < names.length; i += 1) {
      if (names[i].indexOf(M3U_FILE_PREFIX) === 0) {
        safeUnlink(pathModule.join(dir, names[i]));
      }
    }
  });
}

function fetchText(target, redirectsLeft, callback) {
  var parsed;
  var transport;
  var request;
  var completed = false;

  function done(error, text, finalUrl) {
    if (completed) {
      return;
    }
    completed = true;
    callback(error, text, finalUrl);
  }

  if (!validUrl(target)) {
    done(new Error("Only HTTP/HTTPS URLs are accepted."));
    return;
  }

  parsed = urlModule.parse(target);
  transport = parsed.protocol === "https:" ? https : http;

  request = transport.get({
    protocol: parsed.protocol,
    hostname: parsed.hostname,
    port: parsed.port,
    path: parsed.path,
    headers: {
      "User-Agent": "Blazzing-webOS/0.9",
      "Accept": "application/json, application/x-mpegURL, application/vnd.apple.mpegurl, text/plain, */*",
      "Accept-Encoding": "identity"
    }
  }, function (response) {
    var chunks = [];
    var total = 0;
    var location;

    if (response.statusCode >= 300 && response.statusCode < 400 &&
        response.headers.location) {
      response.resume();
      if (redirectsLeft <= 0) {
        done(new Error("Too many redirects."));
        return;
      }
      location = urlModule.resolve(target, response.headers.location);
      completed = true;
      fetchText(location, redirectsLeft - 1, callback);
      return;
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
      response.resume();
      done(new Error("Provider returned HTTP " + response.statusCode + "."));
      return;
    }

    response.on("data", function (chunk) {
      total += chunk.length;
      if (total > MAX_TEXT_BYTES) {
        request.abort();
        done(new Error("Provider response exceeds the 8 MiB API limit."));
        return;
      }
      chunks.push(chunk);
    });

    response.on("end", function () {
      done(null, Buffer.concat(chunks).toString("utf8"), target);
    });

    response.on("error", function (error) {
      done(error);
    });
  });

  request.setTimeout(TIMEOUT_MS, function () {
    request.abort();
    done(new Error("Provider request timed out."));
  });

  request.on("error", function (error) {
    done(error);
  });
}

function fetchArtworkBinary(target, redirectsLeft, callback) {
  var parsed;
  var transport;
  var request;
  var completed = false;

  function done(error, result) {
    if (completed) {
      return;
    }
    completed = true;
    callback(error, result);
  }

  if (!validUrl(target)) {
    done(new Error("Only HTTP/HTTPS artwork URLs are accepted."));
    return;
  }

  parsed = urlModule.parse(target);
  transport = parsed.protocol === "https:" ? https : http;

  request = transport.get({
    protocol: parsed.protocol,
    hostname: parsed.hostname,
    port: parsed.port,
    path: parsed.path,
    headers: {
      "User-Agent": "Blazzing-webOS/0.14",
      "Accept": "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8",
      "Accept-Encoding": "identity"
    }
  }, function (response) {
    var chunks = [];
    var total = 0;
    var location;
    var contentLength;
    var contentType = String(response.headers["content-type"] || "")
      .split(";")[0]
      .trim()
      .toLowerCase();

    if (response.statusCode >= 300 && response.statusCode < 400 &&
        response.headers.location) {
      response.resume();
      if (redirectsLeft <= 0) {
        done(new Error("Too many artwork redirects."));
        return;
      }

      location = urlModule.resolve(target, response.headers.location);
      completed = true;
      fetchArtworkBinary(location, redirectsLeft - 1, callback);
      return;
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
      response.resume();
      done(new Error("Artwork provider returned HTTP " + response.statusCode + "."));
      return;
    }

    if (contentType &&
        contentType.indexOf("image/") !== 0 &&
        contentType !== "application/octet-stream") {
      response.resume();
      done(new Error("Artwork response is not an image."));
      return;
    }

    contentLength = Number(response.headers["content-length"] || 0);
    if (contentLength > MAX_ARTWORK_BYTES) {
      response.resume();
      done(new Error("Artwork exceeds the 1 MiB cache limit."));
      return;
    }

    response.on("data", function (chunk) {
      total += chunk.length;
      if (total > MAX_ARTWORK_BYTES) {
        request.abort();
        done(new Error("Artwork exceeds the 1 MiB cache limit."));
        return;
      }
      chunks.push(chunk);
    });

    response.on("end", function () {
      var data = Buffer.concat(chunks);

      done(null, {
        mime: contentType || "application/octet-stream",
        size: data.length,
        base64: data.toString("base64")
      });
    });

    response.on("error", function (error) {
      done(error);
    });
  });

  request.setTimeout(TIMEOUT_MS, function () {
    request.abort();
    done(new Error("Artwork request timed out."));
  });

  request.on("error", function (error) {
    done(error);
  });
}

function downloadM3U(target, redirectsLeft, sessionId, callback) {
  var parsed;
  var transport;
  var request;
  var completed = false;
  var responseFail = null;

  function done(error, result) {
    if (completed) {
      return;
    }
    completed = true;
    callback(error, result);
  }

  if (!validUrl(target)) {
    done(new Error("Only HTTP/HTTPS URLs are accepted."));
    return;
  }

  parsed = urlModule.parse(target);
  transport = parsed.protocol === "https:" ? https : http;

  request = transport.get({
    protocol: parsed.protocol,
    hostname: parsed.hostname,
    port: parsed.port,
    path: parsed.path,
    headers: {
      "User-Agent": "Blazzing-webOS/0.9",
      "Accept": "application/x-mpegURL, application/vnd.apple.mpegurl, text/plain, */*",
      "Accept-Encoding": "identity"
    }
  }, function (response) {
    var location;
    var contentLength;
    var filePath;
    var output;
    var scanner;
    var total = 0;
    var failed = false;

    function fail(error) {
      if (failed || completed) {
        return;
      }

      failed = true;
      try {
        response.destroy();
      } catch (ignoreResponseError) {
        // Best effort.
      }

      if (output) {
        try {
          output.destroy();
        } catch (ignoreOutputError) {
          // Best effort.
        }
      }

      safeUnlink(filePath);
      done(error);
    }

    responseFail = fail;

    if (response.statusCode >= 300 && response.statusCode < 400 &&
        response.headers.location) {
      response.resume();
      if (redirectsLeft <= 0) {
        done(new Error("Too many redirects."));
        return;
      }

      location = urlModule.resolve(target, response.headers.location);
      completed = true;
      downloadM3U(location, redirectsLeft - 1, sessionId, callback);
      return;
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
      response.resume();
      done(new Error("Provider returned HTTP " + response.statusCode + "."));
      return;
    }

    contentLength = Number(response.headers["content-length"] || 0);
    if (contentLength > MAX_M3U_BYTES) {
      response.resume();
      done(new Error("Playlist exceeds the 128 MiB limit."));
      return;
    }

    filePath = pathModule.join(
      os.tmpdir(),
      M3U_FILE_PREFIX + sessionId + ".m3u"
    );
    scanner = m3uCatalog.createMetadataScanner(target);
    output = fs.createWriteStream(filePath, {
      flags: "wx",
      mode: 384
    });

    output.on("error", function (error) {
      fail(error);
    });

    output.on("finish", function () {
      var metadata;

      if (failed || completed) {
        return;
      }

      try {
        metadata = scanner.finish();
      } catch (error) {
        fail(error);
        return;
      }

      done(null, {
        filePath: filePath,
        finalUrl: target,
        totalBytes: total,
        groups: metadata.groups,
        itemCount: metadata.itemCount
      });
    });

    response.on("data", function (chunk) {
      var writable;

      if (failed || completed) {
        return;
      }

      total += chunk.length;
      if (total > MAX_M3U_BYTES) {
        fail(new Error("Playlist exceeds the 128 MiB limit."));
        return;
      }

      try {
        scanner.feed(chunk);
      } catch (error) {
        fail(error);
        return;
      }

      writable = output.write(chunk);
      if (!writable) {
        response.pause();
        output.once("drain", function () {
          if (!failed && !completed) {
            response.resume();
          }
        });
      }
    });

    response.on("end", function () {
      if (!failed && !completed) {
        output.end();
      }
    });

    response.on("error", function (error) {
      fail(error);
    });
  });

  request.setTimeout(TIMEOUT_MS, function () {
    var error = new Error("Provider request timed out.");
    request.abort();

    if (responseFail) {
      responseFail(error);
    } else {
      done(error);
    }
  });

  request.on("error", function (error) {
    if (responseFail) {
      responseFail(error);
    } else {
      done(error);
    }
  });
}

service.register("fetchM3U", function (message) {
  var target = message.payload && message.payload.url;

  if (!validUrl(target)) {
    message.respond({
      returnValue: false,
      errorText: "Invalid M3U URL."
    });
    return;
  }

  fetchText(target, MAX_REDIRECTS, function (error, text, finalUrl) {
    if (error) {
      message.respond({
        returnValue: false,
        errorText: error.message || "Network request failed."
      });
      return;
    }

    message.respond({
      returnValue: true,
      text: text,
      finalUrl: finalUrl
    });
  });
});

service.register("fetchArtwork", function (message) {
  var target = message.payload && message.payload.url;

  if (!validUrl(target)) {
    message.respond({
      returnValue: false,
      errorText: "Invalid artwork URL."
    });
    return;
  }

  fetchArtworkBinary(target, MAX_REDIRECTS, function (error, result) {
    if (error) {
      message.respond({
        returnValue: false,
        errorText: error.message || "Artwork request failed."
      });
      return;
    }

    message.respond({
      returnValue: true,
      mime: result.mime,
      size: result.size,
      base64: result.base64,
      maxBytes: MAX_ARTWORK_BYTES
    });
  });
});

service.register("prepareM3U", function (message) {
  var target = message.payload && message.payload.url;
  var sessionId;

  cleanupSessions();

  if (!validUrl(target)) {
    message.respond({
      returnValue: false,
      errorText: "Invalid M3U URL."
    });
    return;
  }

  sessionId = makeSessionId();

  downloadM3U(target, MAX_REDIRECTS, sessionId, function (error, result) {
    if (error) {
      message.respond({
        returnValue: false,
        errorText: error.message || "Playlist download failed."
      });
      return;
    }

    m3uSessions[sessionId] = {
      filePath: result.filePath,
      finalUrl: result.finalUrl,
      totalBytes: result.totalBytes,
      itemCount: result.itemCount,
      groups: result.groups,
      touchedAt: Date.now()
    };

    message.respond({
      returnValue: true,
      sessionId: sessionId,
      finalUrl: result.finalUrl,
      totalBytes: result.totalBytes,
      itemCount: result.itemCount,
      groups: result.groups,
      maxBytes: MAX_M3U_BYTES
    });
  });
});

service.register("queryM3U", function (message) {
  var payload = message.payload || {};
  var sessionId = String(payload.sessionId || "");
  var session;

  cleanupSessions();
  session = m3uSessions[sessionId];

  if (!session) {
    message.respond({
      returnValue: false,
      errorText: "M3U session expired or does not exist."
    });
    return;
  }

  session.touchedAt = Date.now();

  m3uCatalog.queryPage({
    filePath: session.filePath,
    fileSize: session.totalBytes,
    baseUrl: session.finalUrl,
    startOffset: Number(payload.startOffset || 0),
    limit: Number(payload.limit || 48),
    group: String(payload.group || ""),
    query: String(payload.query || ""),
    favoritesOnly: payload.favoritesOnly === true,
    favoriteIds: Array.isArray(payload.favoriteIds) ? payload.favoriteIds : []
  }, function (error, result) {
    if (error) {
      message.respond({
        returnValue: false,
        errorText: error.message || "Playlist query failed."
      });
      return;
    }

    message.respond({
      returnValue: true,
      items: result.items,
      hasMore: result.hasMore,
      nextOffset: result.nextOffset
    });
  });
});

service.register("releaseM3U", function (message) {
  var sessionId = String(message.payload && message.payload.sessionId || "");
  var session = m3uSessions[sessionId];

  if (session) {
    safeUnlink(session.filePath);
    delete m3uSessions[sessionId];
  }

  message.respond({
    returnValue: true
  });
});

service.register("plutoLive", function (message) {
  plutoService.live(function (error, result) {
    if (error) {
      message.respond({
        returnValue: false,
        errorText: error.message || "Pluto live request failed."
      });
      return;
    }

    message.respond({
      returnValue: true,
      channels: result.channels,
      categories: result.categories,
      mode: result.mode
    });
  });
});

service.register("plutoStream", function (message) {
  var channelId = String(
    message.payload && message.payload.channelId || ""
  );

  plutoService.stream(channelId, function (error, result) {
    if (error) {
      message.respond({
        returnValue: false,
        errorText: error.message || "Pluto stream request failed."
      });
      return;
    }

    message.respond({
      returnValue: true,
      url: result.url
    });
  });
});

service.register("plutoVod", function (message) {
  plutoService.vod(function (error, payload) {
    if (error) {
      message.respond({
        returnValue: false,
        errorText: error.message || "Pluto VOD request failed."
      });
      return;
    }

    message.respond({
      returnValue: true,
      payload: payload
    });
  });
});

service.register("plutoSeries", function (message) {
  var seriesId = String(
    message.payload && message.payload.seriesId || ""
  );

  plutoService.series(seriesId, function (error, payload) {
    if (error) {
      message.respond({
        returnValue: false,
        errorText: error.message || "Pluto series request failed."
      });
      return;
    }

    message.respond({
      returnValue: true,
      payload: payload
    });
  });
});

service.register("plutoVodStream", function (message) {
  var path = String(
    message.payload && message.payload.path || ""
  );

  plutoService.vodStream(path, function (error, result) {
    if (error) {
      message.respond({
        returnValue: false,
        errorText: error.message || "Pluto VOD stream request failed."
      });
      return;
    }

    message.respond({
      returnValue: true,
      url: result.url
    });
  });
});

service.register("xtreamRequest", function (message) {
  var payload = message.payload || {};
  var server = normalizeServer(payload.server);
  var username = String(payload.username || "");
  var password = String(payload.password || "");
  var action = String(payload.action || "");
  var seriesId = String(payload.seriesId || "");
  var vodId = String(payload.vodId || "");
  var target;

  if (!server || !username || !password || !XTREAM_ACTIONS[action] ||
      (action === "get_series_info" && !seriesId) ||
      (action === "get_vod_info" && !vodId)) {
    message.respond({
      returnValue: false,
      errorText: "Invalid Xtream request."
    });
    return;
  }

  target = server + "player_api.php?username=" +
    encodeURIComponent(username) + "&password=" +
    encodeURIComponent(password);

  if (action) {
    target += "&action=" + encodeURIComponent(action);
  }

  if (action === "get_series_info") {
    target += "&series_id=" + encodeURIComponent(seriesId);
  }

  if (action === "get_vod_info") {
    target += "&vod_id=" + encodeURIComponent(vodId);
  }

  fetchText(target, MAX_REDIRECTS, function (error, text) {
    if (error) {
      message.respond({
        returnValue: false,
        errorText: error.message || "Xtream request failed."
      });
      return;
    }

    message.respond({
      returnValue: true,
      text: text
    });
  });
});

cleanupOrphanedFiles();
