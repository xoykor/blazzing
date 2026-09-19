(function (global) {
  "use strict";

  function normalizeServer(value) {
    var server = String(value || "").trim();

    if (!/^https?:\/\//i.test(server)) {
      throw new Error("Servidor Xtream deve começar com http:// ou https://.");
    }

    server = server.replace(/\/+$/, "");
    return server + "/";
  }

  function credentials(server, username, password) {
    var user = String(username || "");
    var pass = String(password || "");

    if (!user || !pass) {
      throw new Error("Informe usuário e senha.");
    }

    return {
      server: normalizeServer(server),
      username: user,
      password: pass
    };
  }

  function apiUrl(creds, action) {
    var url = creds.server + "player_api.php?username=" +
      encodeURIComponent(creds.username) + "&password=" +
      encodeURIComponent(creds.password);

    if (action) {
      url += "&action=" + encodeURIComponent(action);
    }

    return url;
  }

  function authAccepted(payload) {
    var user;
    var auth;
    var status;

    if (!payload || typeof payload !== "object") {
      return false;
    }

    user = payload.user_info;
    if (!user || typeof user !== "object") {
      return false;
    }

    auth = user.auth;
    status = String(user.status || "").toLowerCase();

    return auth === 1 || auth === "1" || auth === true || status === "active";
  }

  function liveUrl(creds, streamId, directSource) {
    if (/^https?:\/\//i.test(String(directSource || ""))) {
      return directSource;
    }

    return creds.server + "live/" +
      encodeURIComponent(creds.username) + "/" +
      encodeURIComponent(creds.password) + "/" +
      encodeURIComponent(String(streamId)) + ".ts";
  }

  function buildLiveCatalog(categories, streams, creds) {
    var categoryNames = {};
    var groups = [];
    var seenGroups = {};
    var items = [];
    var i;
    var row;
    var id;
    var name;
    var categoryId;
    var group;

    if (!Array.isArray(categories) || !Array.isArray(streams)) {
      throw new Error("Provider Xtream retornou um catálogo inválido.");
    }

    for (i = 0; i < categories.length; i += 1) {
      row = categories[i] || {};
      id = String(row.category_id == null ? "" : row.category_id);
      name = String(row.category_name || "").trim();
      if (id && name) {
        categoryNames[id] = name;
      }
    }

    for (i = 0; i < streams.length; i += 1) {
      row = streams[i] || {};

      if (row.stream_type && String(row.stream_type).toLowerCase() !== "live") {
        continue;
      }

      id = row.stream_id;
      name = String(row.name || "").trim();

      if ((id == null || id === "") || !name) {
        continue;
      }

      categoryId = String(row.category_id == null ? "" : row.category_id);
      group = categoryNames[categoryId] || "Sem categoria";

      items.push({
        index: items.length,
        title: name,
        group: group,
        logo: String(row.stream_icon || ""),
        url: liveUrl(creds, id, row.direct_source)
      });

      if (!seenGroups[group]) {
        seenGroups[group] = true;
        groups.push(group);
      }
    }

    groups.sort(function (a, b) {
      return a.toLowerCase().localeCompare(b.toLowerCase());
    });

    return {
      items: items,
      groups: groups
    };
  }

  global.BlazzingXtream = {
    credentials: credentials,
    apiUrl: apiUrl,
    authAccepted: authAccepted,
    buildLiveCatalog: buildLiveCatalog
  };
}(window));
