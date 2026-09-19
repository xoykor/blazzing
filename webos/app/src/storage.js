(function (global) {
  "use strict";

  var STORAGE_KEY = "blazzing.webos.favorites.v1";
  var PROGRESS_KEY = "blazzing.webos.progress.v1";
  var XTREAM_PROFILE_KEY = "blazzing.webos.xtream-profile.v1";
  var MAX_PROGRESS_ITEMS = 200;

  function fingerprint(value) {
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

  function emptyState() {
    return {
      version: 1,
      favorites: {}
    };
  }

  function storageAvailable() {
    try {
      return !!global.localStorage;
    } catch (error) {
      return false;
    }
  }

  function load() {
    var raw;
    var parsed;

    if (!storageAvailable()) {
      return emptyState();
    }

    try {
      raw = global.localStorage.getItem(STORAGE_KEY);
      if (!raw) {
        return emptyState();
      }

      parsed = JSON.parse(raw);
      if (!parsed || typeof parsed !== "object" ||
          !parsed.favorites || typeof parsed.favorites !== "object") {
        return emptyState();
      }

      return {
        version: 1,
        favorites: parsed.favorites
      };
    } catch (error) {
      return emptyState();
    }
  }

  function save(state) {
    if (!storageAvailable()) {
      return false;
    }

    try {
      global.localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
      return true;
    } catch (error) {
      return false;
    }
  }

  function isFavorite(key) {
    var state;

    if (!key) {
      return false;
    }

    state = load();
    return Object.prototype.hasOwnProperty.call(state.favorites, String(key));
  }

  function toggle(key, metadata) {
    var state;
    var normalizedKey = String(key || "");
    var data = metadata || {};

    if (!normalizedKey) {
      return false;
    }

    state = load();

    if (Object.prototype.hasOwnProperty.call(state.favorites, normalizedKey)) {
      delete state.favorites[normalizedKey];
      save(state);
      return false;
    }

    state.favorites[normalizedKey] = {
      title: String(data.title || ""),
      group: String(data.group || ""),
      kind: String(data.kind || ""),
      addedAt: Date.now()
    };

    save(state);
    return true;
  }

  function count() {
    return Object.keys(load().favorites).length;
  }

  function favoriteKeys(prefix) {
    var favorites = load().favorites;
    var keys = Object.keys(favorites);
    var wanted = String(prefix || "");
    var result = [];
    var i;

    for (i = 0; i < keys.length; i += 1) {
      if (!wanted || keys[i].indexOf(wanted) === 0) {
        result.push(keys[i]);
      }
    }

    return result;
  }

  function getXtreamProfile() {
    var raw;
    var parsed;

    if (!storageAvailable()) {
      return null;
    }

    try {
      raw = global.localStorage.getItem(XTREAM_PROFILE_KEY);
      if (!raw) {
        return null;
      }

      parsed = JSON.parse(raw);
      if (!parsed || typeof parsed !== "object") {
        return null;
      }

      if (!/^https?:\/\//i.test(String(parsed.server || "")) ||
          !String(parsed.username || "")) {
        return null;
      }

      return {
        server: String(parsed.server),
        username: String(parsed.username)
      };
    } catch (error) {
      return null;
    }
  }

  function setXtreamProfile(server, username) {
    var normalizedServer = String(server || "");
    var normalizedUser = String(username || "");

    if (!storageAvailable() ||
        !/^https?:\/\//i.test(normalizedServer) ||
        !normalizedUser) {
      return false;
    }

    try {
      global.localStorage.setItem(
        XTREAM_PROFILE_KEY,
        JSON.stringify({
          server: normalizedServer,
          username: normalizedUser
        })
      );
      return true;
    } catch (error) {
      return false;
    }
  }

  function clearXtreamProfile() {
    if (!storageAvailable()) {
      return;
    }

    try {
      global.localStorage.removeItem(XTREAM_PROFILE_KEY);
    } catch (error) {
      // Storage failures must not break the TV app.
    }
  }

  function loadProgress() {
    var raw;
    var parsed;

    if (!storageAvailable()) {
      return {};
    }

    try {
      raw = global.localStorage.getItem(PROGRESS_KEY);
      parsed = raw ? JSON.parse(raw) : {};
      return parsed && typeof parsed === "object" ? parsed : {};
    } catch (error) {
      return {};
    }
  }

  function saveProgress(progress) {
    var keys;
    var oldestKey;
    var oldestAt;
    var i;
    var row;

    if (!storageAvailable()) {
      return false;
    }

    keys = Object.keys(progress);
    while (keys.length > MAX_PROGRESS_ITEMS) {
      oldestKey = "";
      oldestAt = Number.POSITIVE_INFINITY;

      for (i = 0; i < keys.length; i += 1) {
        row = progress[keys[i]] || {};
        if (Number(row.updatedAt || 0) < oldestAt) {
          oldestAt = Number(row.updatedAt || 0);
          oldestKey = keys[i];
        }
      }

      if (!oldestKey) {
        break;
      }

      delete progress[oldestKey];
      keys = Object.keys(progress);
    }

    try {
      global.localStorage.setItem(PROGRESS_KEY, JSON.stringify(progress));
      return true;
    } catch (error) {
      return false;
    }
  }

  function getProgress(key) {
    var row;

    if (!key) {
      return 0;
    }

    row = loadProgress()[String(key)];
    if (!row || !isFinite(Number(row.seconds))) {
      return 0;
    }

    return Math.max(0, Number(row.seconds));
  }

  function clearProgress(key) {
    var progress;

    if (!key) {
      return;
    }

    progress = loadProgress();
    delete progress[String(key)];
    saveProgress(progress);
  }

  function setProgress(key, seconds, duration) {
    var progress;
    var current = Number(seconds);
    var total = Number(duration);

    if (!key || !isFinite(current) || current < 10) {
      return;
    }

    if (isFinite(total) && total > 0 && current >= total - 30) {
      clearProgress(key);
      return;
    }

    progress = loadProgress();
    progress[String(key)] = {
      seconds: Math.floor(current),
      updatedAt: Date.now()
    };
    saveProgress(progress);
  }

  function clear() {
    if (!storageAvailable()) {
      return;
    }

    try {
      global.localStorage.removeItem(STORAGE_KEY);
      global.localStorage.removeItem(PROGRESS_KEY);
      global.localStorage.removeItem(XTREAM_PROFILE_KEY);
    } catch (error) {
      // Storage failures must not break the TV app.
    }
  }

  global.BlazzingStorage = {
    fingerprint: fingerprint,
    isFavorite: isFavorite,
    toggle: toggle,
    count: count,
    favoriteKeys: favoriteKeys,
    getXtreamProfile: getXtreamProfile,
    setXtreamProfile: setXtreamProfile,
    clearXtreamProfile: clearXtreamProfile,
    getProgress: getProgress,
    setProgress: setProgress,
    clearProgress: clearProgress,
    clear: clear
  };
}(window));
