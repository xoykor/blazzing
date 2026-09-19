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

  function apiUrl(creds, action, params) {
    var url = creds.server + "player_api.php?username=" +
      encodeURIComponent(creds.username) + "&password=" +
      encodeURIComponent(creds.password);

    if (action) {
      url += "&action=" + encodeURIComponent(action);
    }

    if (params && params.seriesId != null && params.seriesId !== "") {
      url += "&series_id=" + encodeURIComponent(String(params.seriesId));
    }

    if (params && params.vodId != null && params.vodId !== "") {
      url += "&vod_id=" + encodeURIComponent(String(params.vodId));
    }

    return url;
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

  function favoriteKey(creds, kind, id) {
    return "xtream:" + stableHash(creds.server) + ":" +
      String(kind || "item") + ":" + String(id);
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

  function generatedMediaUrl(creds, route, streamId, extension) {
    var ext = String(extension || "mp4").replace(/^\.+/, "");

    if (!ext) {
      ext = "mp4";
    }

    return creds.server + route + "/" +
      encodeURIComponent(creds.username) + "/" +
      encodeURIComponent(creds.password) + "/" +
      encodeURIComponent(String(streamId)) + "." +
      encodeURIComponent(ext);
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

  function vodUrl(creds, streamId, extension, directSource) {
    if (/^https?:\/\//i.test(String(directSource || ""))) {
      return directSource;
    }

    return generatedMediaUrl(creds, "movie", streamId, extension || "mp4");
  }

  function episodeUrl(creds, streamId, extension) {
    return generatedMediaUrl(creds, "series", streamId, extension || "mp4");
  }

  function makeCategoryMap(categories) {
    var names = {};
    var i;
    var row;
    var id;
    var name;

    if (!Array.isArray(categories)) {
      throw new Error("Provider Xtream retornou categorias inválidas.");
    }

    for (i = 0; i < categories.length; i += 1) {
      row = categories[i] || {};
      id = String(row.category_id == null ? "" : row.category_id);
      name = String(row.category_name || "").trim();
      if (id && name) {
        names[id] = name;
      }
    }

    return names;
  }

  function addGroup(groups, seenGroups, group) {
    if (!seenGroups[group]) {
      seenGroups[group] = true;
      groups.push(group);
    }
  }

  function finishCatalog(items, groups) {
    groups.sort(function (a, b) {
      return a.toLowerCase().localeCompare(b.toLowerCase());
    });

    return {
      items: items,
      groups: groups
    };
  }

  function buildLiveCatalog(categories, streams, creds) {
    var categoryNames = makeCategoryMap(categories);
    var groups = [];
    var seenGroups = {};
    var items = [];
    var i;
    var row;
    var id;
    var name;
    var categoryId;
    var group;

    if (!Array.isArray(streams)) {
      throw new Error("Provider Xtream retornou canais inválidos.");
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
        url: liveUrl(creds, id, row.direct_source),
        fallbackUrl: /^https?:\/\//i.test(String(row.direct_source || "")) ?
          liveUrl(creds, id, "") : "",
        kind: "live",
        favoriteKey: favoriteKey(creds, "live", id)
      });

      addGroup(groups, seenGroups, group);
    }

    return finishCatalog(items, groups);
  }

  function buildVodCatalog(categories, streams, creds) {
    var categoryNames = makeCategoryMap(categories);
    var groups = [];
    var seenGroups = {};
    var items = [];
    var i;
    var row;
    var id;
    var name;
    var categoryId;
    var group;

    if (!Array.isArray(streams)) {
      throw new Error("Provider Xtream retornou filmes inválidos.");
    }

    for (i = 0; i < streams.length; i += 1) {
      row = streams[i] || {};
      id = row.stream_id;
      name = String(row.name || row.title || "").trim();

      if ((id == null || id === "") || !name) {
        continue;
      }

      categoryId = String(row.category_id == null ? "" : row.category_id);
      group = categoryNames[categoryId] || "Sem categoria";

      items.push({
        index: items.length,
        title: name,
        group: group,
        logo: String(row.stream_icon || row.cover || ""),
        url: vodUrl(
          creds,
          id,
          row.container_extension || "mp4",
          row.direct_source
        ),
        fallbackUrl: /^https?:\/\//i.test(String(row.direct_source || "")) ?
          vodUrl(
            creds,
            id,
            row.container_extension || "mp4",
            ""
          ) : "",
        kind: "vod",
        vodId: String(id),
        favoriteKey: favoriteKey(creds, "vod", id)
      });

      addGroup(groups, seenGroups, group);
    }

    return finishCatalog(items, groups);
  }

  function buildVodMetadata(payload, fallback) {
    var data = payload && typeof payload === "object" ? payload : {};
    var info = data.info && typeof data.info === "object" ? data.info : {};
    var movie = data.movie_data && typeof data.movie_data === "object" ?
      data.movie_data : {};
    var base = fallback || {};

    return {
      title: String(movie.name || info.name || base.title || "Filme"),
      plot: String(info.plot || info.description || ""),
      genre: String(info.genre || ""),
      year: String(info.releasedate || info.release_date || info.year || ""),
      rating: String(info.rating_5based || info.rating || ""),
      duration: String(info.duration || ""),
      logo: String(info.movie_image || movie.stream_icon || base.logo || "")
    };
  }

  function buildSeriesMetadata(payload, fallback) {\n    var data = payload && typeof payload === "object" ? payload : {};\n    var info = data.info && typeof data.info === "object" ? data.info : {};\n    var base = fallback || {};\n\n    return {\n      title: String(info.name || base.title || "Série"),\n      plot: String(info.plot || info.description || ""),\n      genre: String(info.genre || ""),\n      year: String(info.release_date || info.releasedate || info.year || ""),\n      rating: String(info.rating_5based || info.rating || ""),\n      duration: String(info.episode_run_time || info.duration || ""),\n      logo: String(info.cover || info.cover_big || base.logo || "")\n    };\n  }\n\n  function buildSeriesCatalog(categories, series, creds) {
    var categoryNames = makeCategoryMap(categories);
    var groups = [];
    var seenGroups = {};
    var items = [];
    var i;
    var row;
    var id;
    var name;
    var categoryId;
    var group;

    if (!Array.isArray(series)) {
      throw new Error("Provider Xtream retornou séries inválidas.");
    }

    for (i = 0; i < series.length; i += 1) {
      row = series[i] || {};
      id = row.series_id;
      name = String(row.name || row.title || "").trim();

      if ((id == null || id === "") || !name) {
        continue;
      }

      categoryId = String(row.category_id == null ? "" : row.category_id);
      group = categoryNames[categoryId] || "Sem categoria";

      items.push({
        index: items.length,
        title: name,
        group: group,
        logo: String(row.cover || row.stream_icon || ""),
        url: "",
        kind: "series",
        seriesId: String(id),
        favoriteKey: favoriteKey(creds, "series", id)
      });

      addGroup(groups, seenGroups, group);
    }

    return finishCatalog(items, groups);
  }

  function episodeImage(episode) {
    var info = episode && episode.info;
    if (!info || typeof info !== "object") {
      return "";
    }
    return String(info.movie_image || info.cover_big || "");
  }

  function pushEpisode(items, groups, seenGroups, episode, seasonId, seasonName, creds) {
    var id;
    var title;
    var episodeNum;

    if (!episode || typeof episode !== "object") {
      return;
    }

    id = episode.id != null && episode.id !== "" ?
      episode.id : episode.stream_id;

    if (id == null || id === "") {
      return;
    }

    title = String(episode.title || "").trim();
    episodeNum = String(episode.episode_num == null ? "" : episode.episode_num);

    if (!title) {
      title = "Episódio " + (episodeNum || String(id));
    }

    items.push({
      index: items.length,
      title: title,
      group: seasonName,
      seasonId: String(seasonId),
      logo: episodeImage(episode),
      url: episodeUrl(
        creds,
        id,
        episode.container_extension || "mp4"
      ),
      kind: "episode",
      favoriteKey: favoriteKey(creds, "episode", id)
    });

    addGroup(groups, seenGroups, seasonName);
  }

  function buildEpisodeCatalog(payload, creds) {
    var episodes;
    var groups = [];
    var seenGroups = {};
    var items = [];
    var seasonIds;
    var i;
    var j;
    var seasonId;
    var list;
    var seasonName;

    if (!payload || typeof payload !== "object") {
      throw new Error("Detalhes da série Xtream inválidos.");
    }

    episodes = payload.episodes;

    if (Array.isArray(episodes)) {
      seasonName = "Episódios";
      for (i = 0; i < episodes.length; i += 1) {
        pushEpisode(items, groups, seenGroups, episodes[i], "1", seasonName, creds);
      }
      return {
        items: items,
        groups: groups
      };
    }

    if (!episodes || typeof episodes !== "object") {
      throw new Error("Série sem episódios.");
    }

    seasonIds = Object.keys(episodes);
    seasonIds.sort(function (a, b) {
      var na = Number(a);
      var nb = Number(b);
      if (!isNaN(na) && !isNaN(nb)) {
        return na - nb;
      }
      return String(a).localeCompare(String(b));
    });

    for (i = 0; i < seasonIds.length; i += 1) {
      seasonId = seasonIds[i];
      list = episodes[seasonId];
      if (!Array.isArray(list)) {
        continue;
      }
      seasonName = "Temporada " + seasonId;
      for (j = 0; j < list.length; j += 1) {
        pushEpisode(items, groups, seenGroups, list[j], seasonId, seasonName, creds);
      }
    }

    if (!items.length) {
      throw new Error("Nenhum episódio encontrado.");
    }

    return {
      items: items,
      groups: groups
    };
  }

  global.BlazzingXtream = {
    credentials: credentials,
    apiUrl: apiUrl,
    authAccepted: authAccepted,
    buildLiveCatalog: buildLiveCatalog,
    buildVodCatalog: buildVodCatalog,
    buildVodMetadata: buildVodMetadata,
    buildSeriesCatalog: buildSeriesCatalog,
    buildEpisodeCatalog: buildEpisodeCatalog
  };
}(window));
