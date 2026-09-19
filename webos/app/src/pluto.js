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

  global.BlazzingPluto = {
    buildLiveCatalog: buildLiveCatalog
  };
}(window));
