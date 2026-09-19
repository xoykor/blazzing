"use strict";

var Service = require("webos-service");
var http = require("http");
var https = require("https");
var urlModule = require("url");

var service = new Service("io.github.xoykor.blazzing.network");
var MAX_BYTES = 8 * 1024 * 1024;
var MAX_REDIRECTS = 5;
var TIMEOUT_MS = 15000;
var XTREAM_ACTIONS = {
  "": true,
  "get_live_categories": true,
  "get_live_streams": true
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
      "User-Agent": "Blazzing-webOS/0.3",
      "Accept": "application/json, application/x-mpegURL, application/vnd.apple.mpegurl, text/plain, */*"
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
      if (total > MAX_BYTES) {
        request.abort();
        done(new Error("Provider response exceeds the 8 MiB webOS alpha limit."));
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

service.register("xtreamRequest", function (message) {
  var payload = message.payload || {};
  var server = normalizeServer(payload.server);
  var username = String(payload.username || "");
  var password = String(payload.password || "");
  var action = String(payload.action || "");
  var target;

  if (!server || !username || !password || !XTREAM_ACTIONS[action]) {
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
