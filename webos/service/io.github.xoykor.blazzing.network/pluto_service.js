"use strict";

var http = require("http");
var https = require("https");
var urlModule = require("url");

var MAX_JSON_BYTES = 8 * 1024 * 1024;
var MAX_REDIRECTS = 5;
var TIMEOUT_MS = 15000;
var BOOT_URL = "https://boot.pluto.tv/v4/start";
var CHANNELS_URL =
  "https://service-channels.clusters.pluto.tv/v2/guide/channels";
var CATEGORIES_URL =
  "https://service-channels.clusters.pluto.tv/v2/guide/categories";
var LEGACY_CHANNELS_URL = "https://api.pluto.tv/v2/channels.json";
var VOD_URL =
  "https://api.pluto.tv/v3/vod/categories" +
  "?includeItems=true&deviceType=web&offset=1000";
var SERIES_URL = "https://api.pluto.tv/v3/vod/series/";
var USER_AGENT =
  "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 " +
  "(KHTML, like Gecko) Chrome/120.0 Safari/537.36";
var bootCache = null;
var clientId = randomId();

function randomId() {
  return Date.now().toString(36) + "-" +
    Math.floor(Math.random() * 0x7fffffff).toString(36) + "-" +
    Math.floor(Math.random() * 0x7fffffff).toString(36);
}

function validChannelId(value) {
  return /^[A-Za-z0-9_-]{4,128}$/.test(String(value || ""));
}

function requestJson(target, headers, redirectsLeft, callback) {
  var parsed;
  var transport;
  var request;
  var completed = false;

  function done(error, payload) {
    if (completed) {
      return;
    }
    completed = true;
    callback(error, payload);
  }

  try {
    parsed = urlModule.parse(target);
  } catch (error) {
    done(error);
    return;
  }

  if (!parsed || (parsed.protocol !== "https:" && parsed.protocol !== "http:")) {
    done(new Error("Invalid Pluto URL."));
    return;
  }

  transport = parsed.protocol === "https:" ? https : http;

  request = transport.get({
    protocol: parsed.protocol,
    hostname: parsed.hostname,
    port: parsed.port,
    path: parsed.path,
    headers: headers || {}
  }, function (response) {
    var chunks = [];
    var total = 0;
    var location;

    if (response.statusCode >= 300 && response.statusCode < 400 &&
        response.headers.location) {
      response.resume();

      if (redirectsLeft <= 0) {
        done(new Error("Too many Pluto redirects."));
        return;
      }

      location = urlModule.resolve(target, response.headers.location);
      completed = true;
      requestJson(location, headers, redirectsLeft - 1, callback);
      return;
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
      response.resume();
      done(new Error("Pluto returned HTTP " + response.statusCode + "."));
      return;
    }

    response.on("data", function (chunk) {
      total += chunk.length;

      if (total > MAX_JSON_BYTES) {
        request.abort();
        done(new Error("Pluto response exceeds the 8 MiB API limit."));
        return;
      }

      chunks.push(chunk);
    });

    response.on("end", function () {
      var text;
      var payload;

      try {
        text = Buffer.concat(chunks).toString("utf8");
        payload = JSON.parse(text);
      } catch (error) {
        done(new Error("Pluto returned invalid JSON."));
        return;
      }

      done(null, payload);
    });

    response.on("error", function (error) {
      done(error);
    });
  });

  request.setTimeout(TIMEOUT_MS, function () {
    request.abort();
    done(new Error("Pluto request timed out."));
  });

  request.on("error", function (error) {
    done(error);
  });
}

function tokenExpiryMs(token) {
  var parts = String(token || "").split(".");
  var encoded;
  var payload;
  var padding;

  if (parts.length < 2) {
    return 0;
  }

  try {
    encoded = parts[1].replace(/-/g, "+").replace(/_/g, "/");
    padding = encoded.length % 4;

    if (padding) {
      encoded += "====".slice(padding);
    }

    payload = JSON.parse(Buffer.from(encoded, "base64").toString("utf8"));
    return Number(payload.exp || 0) * 1000;
  } catch (error) {
    return 0;
  }
}

function commonHeaders() {
  return {
    "Accept": "application/json, text/plain, */*",
    "Accept-Encoding": "identity",
    "Origin": "https://pluto.tv",
    "Referer": "https://pluto.tv/",
    "User-Agent": USER_AGENT
  };
}

function bootUrl() {
  var params = [
    "appName=web",
    "appVersion=9.1.0",
    "deviceVersion=120.0.0",
    "deviceModel=web",
    "deviceMake=chrome",
    "deviceType=web",
    "clientID=" + encodeURIComponent(clientId),
    "clientModelNumber=1.0.0",
    "serverSideAds=true"
  ];

  return BOOT_URL + "?" + params.join("&");
}

function boot(callback) {
  var now = Date.now();

  if (bootCache &&
      bootCache.sessionToken &&
      bootCache.stitcher &&
      bootCache.expiresAt > now + 60000) {
    callback(null, bootCache);
    return;
  }

  requestJson(
    bootUrl(),
    commonHeaders(),
    MAX_REDIRECTS,
    function (error, payload) {
      var token;
      var stitcher;
      var expiresAt;

      if (error) {
        callback(error);
        return;
      }

      token = String(payload && payload.sessionToken || "");
      stitcher = String(
        payload &&
        payload.servers &&
        payload.servers.stitcher ||
        ""
      ).replace(/\/+$/, "");

      if (!token || !/^https?:\/\//i.test(stitcher)) {
        callback(new Error("Pluto boot response is incomplete."));
        return;
      }

      expiresAt = tokenExpiryMs(token);
      if (!expiresAt || expiresAt <= now) {
        expiresAt = now + 40 * 60 * 1000;
      }

      bootCache = {
        sessionToken: token,
        stitcher: stitcher,
        stitcherParams: String(payload.stitcherParams || ""),
        expiresAt: expiresAt
      };

      callback(null, bootCache);
    }
  );
}

function authHeaders(token) {
  var headers = commonHeaders();
  headers.Authorization = "Bearer " + token;
  return headers;
}

function guideUrl(base) {
  return base +
    "?channelIds=&offset=0&limit=1000&sort=" +
    encodeURIComponent("number:asc");
}

function legacyChannels(callback) {
  var target = LEGACY_CHANNELS_URL +
    "?sid=" + encodeURIComponent(clientId) +
    "&deviceId=" + encodeURIComponent(clientId);

  requestJson(
    target,
    commonHeaders(),
    MAX_REDIRECTS,
    function (error, payload) {
      if (error) {
        callback(error);
        return;
      }

      if (!Array.isArray(payload)) {
        callback(new Error("Pluto legacy channel response is invalid."));
        return;
      }

      callback(null, {
        channels: payload,
        categories: [],
        mode: "legacy"
      });
    }
  );
}

function live(callback) {
  boot(function (bootError, session) {
    var channels = null;
    var categories = [];
    var pending = 2;
    var channelError = null;

    function finishPart() {
      pending -= 1;

      if (pending > 0) {
        return;
      }

      if (channelError || !Array.isArray(channels) || !channels.length) {
        legacyChannels(callback);
        return;
      }

      callback(null, {
        channels: channels,
        categories: categories,
        mode: "guide-v2"
      });
    }

    if (bootError) {
      callback(bootError);
      return;
    }

    requestJson(
      guideUrl(CHANNELS_URL),
      authHeaders(session.sessionToken),
      MAX_REDIRECTS,
      function (error, payload) {
        if (error) {
          channelError = error;
        } else {
          channels = payload && Array.isArray(payload.data) ?
            payload.data : null;
        }
        finishPart();
      }
    );

    requestJson(
      guideUrl(CATEGORIES_URL),
      authHeaders(session.sessionToken),
      MAX_REDIRECTS,
      function (error, payload) {
        if (!error && payload && Array.isArray(payload.data)) {
          categories = payload.data;
        }
        finishPart();
      }
    );
  });
}

function validContentId(value) {
  return /^[A-Za-z0-9_-]{4,128}$/.test(String(value || ""));
}

function vod(callback) {
  boot(function (bootError, session) {
    if (bootError) {
      callback(bootError);
      return;
    }

    requestJson(
      VOD_URL,
      authHeaders(session.sessionToken),
      MAX_REDIRECTS,
      function (error, payload) {
        if (error) {
          callback(error);
          return;
        }

        if (!payload || !Array.isArray(payload.categories)) {
          callback(new Error("Pluto VOD response is invalid."));
          return;
        }

        callback(null, payload);
      }
    );
  });
}

function series(seriesId, callback) {
  if (!validContentId(seriesId)) {
    callback(new Error("Invalid Pluto series ID."));
    return;
  }

  boot(function (bootError, session) {
    var target;

    if (bootError) {
      callback(bootError);
      return;
    }

    target = SERIES_URL +
      encodeURIComponent(String(seriesId)) +
      "/seasons?includeItems=true&deviceType=web";

    requestJson(
      target,
      authHeaders(session.sessionToken),
      MAX_REDIRECTS,
      callback
    );
  });
}

function sanitizeVodPath(value) {
  var text = String(value || "").trim();
  var parsed;
  var path;

  if (!text) {
    return "";
  }

  if (/^https?:\/\//i.test(text)) {
    try {
      parsed = urlModule.parse(text);
      path = String(parsed.pathname || "");
    } catch (error) {
      return "";
    }
  } else {
    path = text.split("?")[0];
  }

  if (path.indexOf("/v2/stitch/") === 0) {
    return path;
  }

  if (path.indexOf("/v1/stitch/") === 0) {
    return "/v2" + path.slice(3);
  }

  if (path.indexOf("/stitch/") === 0) {
    return "/v2" + path;
  }

  return "";
}

function buildVodStreamUrl(session, value) {
  var path = sanitizeVodPath(value);
  var params;
  var extra;

  if (!session ||
      !session.sessionToken ||
      !/^https?:\/\//i.test(String(session.stitcher || "")) ||
      !path) {
    return "";
  }

  params = [
    "jwt=" + encodeURIComponent(session.sessionToken),
    "masterJWTPassthrough=true",
    "includeExtendedEvents=true"
  ];

  extra = String(session.stitcherParams || "")
    .replace(/^[?&]+/, "")
    .trim();

  if (extra) {
    params.push(extra);
  }

  return String(session.stitcher).replace(/\/+$/, "") +
    path +
    "?" +
    params.join("&");
}

function vodStream(value, callback) {
  var path = sanitizeVodPath(value);

  if (!path) {
    callback(new Error("Invalid Pluto VOD path."));
    return;
  }

  boot(function (error, session) {
    var url;

    if (error) {
      callback(error);
      return;
    }

    url = buildVodStreamUrl(session, path);
    if (!url) {
      callback(new Error("Could not build Pluto VOD stream URL."));
      return;
    }

    callback(null, {
      url: url
    });
  });
}

function buildStreamUrl(session, channelId) {
  var params;
  var extra;

  if (!session ||
      !session.sessionToken ||
      !/^https?:\/\//i.test(String(session.stitcher || "")) ||
      !validChannelId(channelId)) {
    return "";
  }

  params = [
    "jwt=" + encodeURIComponent(session.sessionToken),
    "masterJWTPassthrough=true",
    "includeExtendedEvents=true"
  ];

  extra = String(session.stitcherParams || "")
    .replace(/^[?&]+/, "")
    .trim();

  if (extra) {
    params.push(extra);
  }

  return String(session.stitcher).replace(/\/+$/, "") +
    "/v2/stitch/hls/channel/" +
    encodeURIComponent(String(channelId)) +
    "/master.m3u8?" +
    params.join("&");
}

function stream(channelId, callback) {
  if (!validChannelId(channelId)) {
    callback(new Error("Invalid Pluto channel ID."));
    return;
  }

  boot(function (error, session) {
    var url;

    if (error) {
      callback(error);
      return;
    }

    url = buildStreamUrl(session, channelId);
    if (!url) {
      callback(new Error("Could not build Pluto stream URL."));
      return;
    }

    callback(null, {
      url: url
    });
  });
}

module.exports = {
  live: live,
  stream: stream,
  vod: vod,
  series: series,
  vodStream: vodStream,
  buildStreamUrl: buildStreamUrl,
  buildVodStreamUrl: buildVodStreamUrl,
  sanitizeVodPath: sanitizeVodPath,
  tokenExpiryMs: tokenExpiryMs,
  validChannelId: validChannelId
};
