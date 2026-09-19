(function (global) {
  "use strict";

  function addGroup(groups, seen, name) {
    var group = String(name || "Pluto TV").trim() || "Pluto TV";

    if (!seen[group]) {
      seen[group] = true;
      groups.push(group);
    }

    return group;
  }

  function categoryMap(categories) {
    var map = {};
    var i;
    var j;
    var row;
    var name;
    var ids;

    if (!Array.isArray(categories)) {
      return map;
    }

    for (i = 0; i < categories.length; i += 1) {
      row = categories[i] || {};
      name = String(row.name || row.categoryName || "").trim();
      ids = row.channelIDs || row.channelIds || row.channels || [];

      if (!name || !Array.isArray(ids)) {
        continue;
      }

      for (j = 0; j < ids.length; j += 1) {
        map[String(ids[j])] = name;
      }
    }

    return map;
  }

  function imageUrl(row) {
    var direct;
    var images;
    var preferred = "";
    var fallback = "";
    var i;
    var image;
    var type;
    var value;

    direct = row && row.colorLogoPNG && row.colorLogoPNG.path;
    if (/^https?:\/\//i.test(String(direct || ""))) {
      return String(direct);
    }

    images = row && row.images;
    if (!Array.isArray(images)) {
      return "";
    }

    for (i = 0; i < images.length; i += 1) {
      image = images[i] || {};
      value = String(image.url || image.path || "");
      type = String(image.type || "").toLowerCase();

      if (!/^https?:\/\//i.test(value)) {
        continue;
      }

      if (!fallback) {
        fallback = value;
      }

      if (type === "colorlogopng" ||
          type === "colorlogo" ||
          type === "logo") {
        preferred = value;
        break;
      }
    }

    return preferred || fallback;
  }

  function channelId(row) {
    return String(
      row && (
        row.id ||
        row._id ||
        row.channelId ||
        row.channelID
      ) ||
      ""
    );
  }

  function channelNumber(row) {
    var value = row && (
      row.number != null ? row.number :
        (row.channelNumber != null ? row.channelNumber : "")
    );

    return String(value == null ? "" : value);
  }

  function channelName(row) {
    return String(
      row && (
        row.name ||
        row.title ||
        row.channelName
      ) ||
      ""
    ).trim();
  }

  function legacyCategory(row) {
    return String(
      row && (
        row.category ||
        row.categoryName ||
        row.genre
      ) ||
      ""
    ).trim();
  }

  function buildLiveCatalog(payload) {
    var data = payload || {};
    var channels = Array.isArray(data.channels) ? data.channels : [];
    var categories = categoryMap(data.categories);
    var groups = [];
    var seenGroups = {};
    var seenChannels = {};
    var items = [];
    var i;
    var row;
    var id;
    var name;
    var group;

    for (i = 0; i < channels.length; i += 1) {
      row = channels[i] || {};
      id = channelId(row);
      name = channelName(row);

      if (!id || !name || seenChannels[id]) {
        continue;
      }

      seenChannels[id] = true;
      group = categories[id] || legacyCategory(row) || "Pluto TV";
      group = addGroup(groups, seenGroups, group);

      items.push({
        index: items.length,
        title: name,
        group: group,
        logo: imageUrl(row),
        url: "",
        kind: "pluto-live",
        plutoChannelId: id,
        channelNumber: channelNumber(row),
        favoriteKey: "pluto:live:" + id
      });
    }

    groups.sort(function (a, b) {
      return a.toLowerCase().localeCompare(b.toLowerCase());
    });

    return {
      items: items,
      groups: groups,
      providerMode: String(data.mode || "")
    };
  }

  function safeStitchedPath(value) {
    var text = String(value || "").trim();
    var match;
    var path;

    if (!text) {
      return "";
    }

    if (/^https?:\/\//i.test(text)) {
      match = text.match(/^https?:\/\/[^/]+([^?]*)/i);
      path = match ? String(match[1] || "") : "";
    } else {
      path = text.split("?")[0];
    }

    if (path.indexOf("/v2/stitch/") === 0 ||
        path.indexOf("/v1/stitch/") === 0 ||
        path.indexOf("/stitch/") === 0) {
      return path;
    }

    return "";
  }

  function stitchedPath(item) {
    var stitched = item && item.stitched;
    var urls;
    var i;
    var row;
    var path;

    if (!stitched || typeof stitched !== "object") {
      return "";
    }

    urls = stitched.urls;
    if (Array.isArray(urls)) {
      for (i = 0; i < urls.length; i += 1) {
        row = urls[i] || {};
        path = safeStitchedPath(row.url || row.path || "");
        if (path) {
          return path;
        }
      }
    }

    return safeStitchedPath(stitched.url || stitched.path || "");
  }

  function coverUrl(item) {
    var covers = item && item.covers;
    var featured;
    var i;
    var value;

    if (Array.isArray(covers)) {
      for (i = 0; i < covers.length; i += 1) {
        value = String(covers[i] && covers[i].url || "");
        if (/^https?:\/\//i.test(value)) {
          return value;
        }
      }
    }

    featured = item && item.featuredImage;
    value = String(featured && (featured.path || featured.url) || "");
    return /^https?:\/\//i.test(value) ? value : "";
  }

  function durationSeconds(value) {
    var duration = Number(value || 0);

    if (!isFinite(duration) || duration <= 0) {
      return 0;
    }

    if (duration > 10000) {
      duration = duration / 1000;
    }

    return Math.max(0, Math.floor(duration));
  }

  function durationLabel(seconds) {
    var total = durationSeconds(seconds);
    var hours;
    var minutes;

    if (!total) {
      return "";
    }

    hours = Math.floor(total / 3600);
    minutes = Math.floor((total % 3600) / 60);

    if (hours > 0) {
      return hours + "h " + (minutes ? minutes + "min" : "");
    }

    return Math.max(1, minutes) + "min";
  }

  function releaseYear(item) {
    var clip = item && item.clip;
    var value = String(
      item && (
        item.year ||
        item.releaseDate ||
        item.originalReleaseDate
      ) ||
      clip && clip.originalReleaseDate ||
      ""
    );
    var match = value.match(/\b(19|20)\d{2}\b/);

    return match ? match[0] : "";
  }

  function buildVodCatalog(payload) {
    var categories = payload && Array.isArray(payload.categories) ?
      payload.categories : [];
    var groups = [];
    var seenGroups = {};
    var seenItems = {};
    var items = [];
    var i;
    var j;
    var category;
    var rows;
    var row;
    var id;
    var name;
    var type;
    var seasons;
    var isSeries;
    var isMovie;
    var group;
    var path;

    for (i = 0; i < categories.length; i += 1) {
      category = categories[i] || {};
      rows = Array.isArray(category.items) ? category.items : [];

      for (j = 0; j < rows.length; j += 1) {
        row = rows[j] || {};
        id = String(row._id || row.id || "");
        name = String(row.name || row.title || "").trim();

        if (!id || !name || seenItems[id]) {
          continue;
        }

        type = String(row.type || "").toLowerCase();
        seasons = Array.isArray(row.seasonsNumbers) ? row.seasonsNumbers : [];
        isSeries = type === "series" || seasons.length > 0;
        isMovie = type === "movie" || type === "film";

        if (!isSeries && !isMovie) {
          continue;
        }

        path = isMovie ? stitchedPath(row) : "";
        if (isMovie && !path) {
          continue;
        }

        seenItems[id] = true;
        group = String(row.genre || category.name || "Pluto VOD").trim() ||
          "Pluto VOD";
        group = addGroup(groups, seenGroups, group);

        items.push({
          index: items.length,
          title: name,
          group: group,
          logo: coverUrl(row),
          url: "",
          kind: isSeries ? "pluto-series" : "pluto-movie",
          plutoContentId: id,
          plutoSeriesId: isSeries ? id : "",
          plutoVodPath: path,
          plot: String(row.summary || row.description || ""),
          genre: String(row.genre || ""),
          rating: String(row.rating || ""),
          year: releaseYear(row),
          duration: durationLabel(row.duration),
          favoriteKey: "pluto:" + (isSeries ? "series:" : "movie:") + id
        });
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

  function buildVodMetadata(entry) {
    var row = entry || {};

    return {
      title: String(row.title || "Filme"),
      plot: String(row.plot || ""),
      genre: String(row.genre || row.group || ""),
      year: String(row.year || ""),
      rating: String(row.rating || ""),
      duration: String(row.duration || ""),
      logo: String(row.logo || "")
    };
  }

  function buildSeriesMetadata(payload, fallback) {
    var data = payload && typeof payload === "object" ? payload : {};
    var base = fallback || {};

    return {
      title: String(data.name || base.title || "Série"),
      plot: String(data.summary || data.description || base.plot || ""),
      genre: String(data.genre || base.genre || base.group || ""),
      year: releaseYear(data) || String(base.year || ""),
      rating: String(data.rating || base.rating || ""),
      duration: String(base.duration || ""),
      logo: coverUrl(data) || String(base.logo || "")
    };
  }

  function buildEpisodeCatalog(payload, seriesEntry) {
    var data = payload && typeof payload === "object" ? payload : {};
    var seasons = Array.isArray(data.seasons) ? data.seasons : [];
    var items = [];
    var groups = [];
    var seenGroups = {};
    var i;
    var j;
    var season;
    var episodes;
    var seasonNumber;
    var group;
    var episode;
    var id;
    var path;
    var episodeNumber;
    var title;

    for (i = 0; i < seasons.length; i += 1) {
      season = seasons[i] || {};
      episodes = Array.isArray(season.episodes) ? season.episodes : [];
      seasonNumber = String(
        season.number != null ? season.number : (i + 1)
      );
      group = addGroup(
        groups,
        seenGroups,
        "Temporada " + seasonNumber
      );

      for (j = 0; j < episodes.length; j += 1) {
        episode = episodes[j] || {};
        id = String(episode._id || episode.id || "");
        path = stitchedPath(episode);

        if (!id || !path) {
          continue;
        }

        episodeNumber = String(
          episode.number != null ? episode.number : (j + 1)
        );
        title = String(episode.name || episode.title || "").trim();

        if (!title) {
          title = "Episódio " + episodeNumber;
        }

        items.push({
          index: items.length,
          title: title,
          group: group,
          logo: coverUrl(episode) ||
            String(seriesEntry && seriesEntry.logo || ""),
          url: "",
          kind: "pluto-episode",
          plutoContentId: id,
          plutoVodPath: path,
          plot: String(episode.summary || episode.description || ""),
          genre: String(episode.genre || data.genre || ""),
          rating: String(episode.rating || ""),
          year: releaseYear(episode),
          duration: durationLabel(episode.duration),
          favoriteKey: "pluto:episode:" + id
        });
      }
    }

    return {
      items: items,
      groups: groups
    };
  }

  global.BlazzingPluto = {
    buildLiveCatalog: buildLiveCatalog,
    buildVodCatalog: buildVodCatalog,
    buildVodMetadata: buildVodMetadata,
    buildSeriesMetadata: buildSeriesMetadata,
    buildEpisodeCatalog: buildEpisodeCatalog,
    safeStitchedPath: safeStitchedPath
  };
}(window));
