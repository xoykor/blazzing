(function (global) {
  "use strict";

  var STORAGE_KEY = "blazzing.webos.favorites.v1";

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

  function clear() {
    if (!storageAvailable()) {
      return;
    }

    try {
      global.localStorage.removeItem(STORAGE_KEY);
    } catch (error) {
      // Storage failures must not break the TV app.
    }
  }

  global.BlazzingStorage = {
    fingerprint: fingerprint,
    isFavorite: isFavorite,
    toggle: toggle,
    count: count,
    clear: clear
  };
}(window));
