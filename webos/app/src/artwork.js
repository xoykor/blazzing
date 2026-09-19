(function (global) {
  "use strict";

  var DB_NAME = "blazzing-webos-artwork-v1";
  var STORE_NAME = "images";
  var DB_VERSION = 1;
  var MAX_IMAGE_BYTES = 1024 * 1024;
  var MAX_CACHE_BYTES = 24 * 1024 * 1024;
  var MAX_CACHE_ITEMS = 256;
  var MAX_AGE_MS = 7 * 24 * 60 * 60 * 1000;
  var TOUCH_INTERVAL_MS = 60 * 60 * 1000;
  var dbPromise = null;

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

  function cacheKey(url) {
    return "art:" + fingerprint(url);
  }

  function validUrl(value) {
    return /^https?:\/\//i.test(String(value || ""));
  }

  function indexedDbAvailable() {
    try {
      return !!global.indexedDB;
    } catch (error) {
      return false;
    }
  }

  function openDb() {
    if (dbPromise) {
      return dbPromise;
    }

    dbPromise = new Promise(function (resolve, reject) {
      var request;

      if (!indexedDbAvailable()) {
        reject(new Error("IndexedDB indisponível."));
        return;
      }

      try {
        request = global.indexedDB.open(DB_NAME, DB_VERSION);
      } catch (error) {
        reject(error);
        return;
      }

      request.onupgradeneeded = function () {
        var db = request.result;

        if (!db.objectStoreNames.contains(STORE_NAME)) {
          db.createObjectStore(STORE_NAME, {
            keyPath: "key"
          });
        }
      };

      request.onsuccess = function () {
        resolve(request.result);
      };

      request.onerror = function () {
        reject(request.error || new Error("Falha ao abrir cache de artwork."));
      };

      request.onblocked = function () {
        reject(new Error("Cache de artwork bloqueado."));
      };
    }).catch(function (error) {
      dbPromise = null;
      throw error;
    });

    return dbPromise;
  }

  function getRecord(db, key) {
    return new Promise(function (resolve, reject) {
      var request;

      try {
        request = db
          .transaction(STORE_NAME, "readonly")
          .objectStore(STORE_NAME)
          .get(key);
      } catch (error) {
        reject(error);
        return;
      }

      request.onsuccess = function () {
        resolve(request.result || null);
      };

      request.onerror = function () {
        reject(request.error || new Error("Falha ao ler artwork."));
      };
    });
  }

  function putRecord(db, record) {
    return new Promise(function (resolve, reject) {
      var transaction;
      var request;

      try {
        transaction = db.transaction(STORE_NAME, "readwrite");
        request = transaction.objectStore(STORE_NAME).put(record);
      } catch (error) {
        reject(error);
        return;
      }

      request.onerror = function () {
        reject(request.error || new Error("Falha ao salvar artwork."));
      };

      transaction.oncomplete = function () {
        resolve();
      };

      transaction.onerror = function () {
        reject(transaction.error || new Error("Falha ao salvar artwork."));
      };
    });
  }

  function deleteKeys(db, keys) {
    if (!keys.length) {
      return Promise.resolve();
    }

    return new Promise(function (resolve, reject) {
      var transaction;
      var store;
      var i;

      try {
        transaction = db.transaction(STORE_NAME, "readwrite");
        store = transaction.objectStore(STORE_NAME);

        for (i = 0; i < keys.length; i += 1) {
          store.delete(keys[i]);
        }
      } catch (error) {
        reject(error);
        return;
      }

      transaction.oncomplete = function () {
        resolve();
      };

      transaction.onerror = function () {
        reject(transaction.error || new Error("Falha ao podar artwork."));
      };
    });
  }

  function prune(db) {
    return new Promise(function (resolve, reject) {
      var transaction;
      var store;
      var request;
      var rows = [];

      try {
        transaction = db.transaction(STORE_NAME, "readonly");
        store = transaction.objectStore(STORE_NAME);
        request = store.openCursor();
      } catch (error) {
        reject(error);
        return;
      }

      request.onsuccess = function (event) {
        var cursor = event.target.result;
        var row;

        if (!cursor) {
          resolve(rows);
          return;
        }

        row = cursor.value || {};
        rows.push({
          key: String(row.key || ""),
          size: Math.max(0, Number(row.size || 0)),
          lastUsed: Number(row.lastUsed || row.cachedAt || 0)
        });
        cursor.continue();
      };

      request.onerror = function () {
        reject(request.error || new Error("Falha ao inspecionar artwork."));
      };
    }).then(function (rows) {
      var totalBytes = 0;
      var remove = [];
      var i;

      rows.sort(function (a, b) {
        return a.lastUsed - b.lastUsed;
      });

      for (i = 0; i < rows.length; i += 1) {
        totalBytes += rows[i].size;
      }

      i = 0;
      while ((rows.length - remove.length > MAX_CACHE_ITEMS ||
              totalBytes > MAX_CACHE_BYTES) &&
             i < rows.length) {
        remove.push(rows[i].key);
        totalBytes -= rows[i].size;
        i += 1;
      }

      return deleteKeys(db, remove);
    });
  }

  function touch(db, record) {
    var now = Date.now();

    if (!record ||
        now - Number(record.lastUsed || 0) < TOUCH_INTERVAL_MS) {
      return;
    }

    record.lastUsed = now;
    putRecord(db, record).catch(function () {
      // Cache metadata updates are best effort.
    });
  }

  function objectSource(blob, directUrl) {
    if (blob &&
        global.URL &&
        typeof global.URL.createObjectURL === "function") {
      return {
        source: global.URL.createObjectURL(blob),
        objectUrl: true,
        cached: true
      };
    }

    return {
      source: directUrl,
      objectUrl: false,
      cached: false
    };
  }

  function fetchAndStore(db, key, url) {
    return global.BlazzingNetwork.fetchArtwork(url)
      .then(function (payload) {
        var blob = payload && payload.blob;
        var size = Number(payload && payload.size || blob && blob.size || 0);
        var now = Date.now();
        var record;

        if (!blob || size <= 0 || size > MAX_IMAGE_BYTES) {
          throw new Error("Artwork inválido para cache.");
        }

        record = {
          key: key,
          blob: blob,
          size: size,
          cachedAt: now,
          lastUsed: now
        };

        return putRecord(db, record)
          .then(function () {
            prune(db).catch(function () {
              // LRU pruning is best effort; quota errors fall back to direct URLs.
            });

            return objectSource(blob, url);
          });
      });
  }

  function load(url) {
    var sourceUrl = String(url || "");
    var key = cacheKey(sourceUrl);

    if (!validUrl(sourceUrl)) {
      return Promise.resolve({
        source: "",
        objectUrl: false,
        cached: false
      });
    }

    return openDb()
      .then(function (db) {
        return getRecord(db, key)
          .then(function (record) {
            var now = Date.now();

            if (record &&
                record.blob &&
                Number(record.size || 0) > 0 &&
                Number(record.size || 0) <= MAX_IMAGE_BYTES &&
                now - Number(record.cachedAt || 0) <= MAX_AGE_MS) {
              touch(db, record);
              return objectSource(record.blob, sourceUrl);
            }

            return fetchAndStore(db, key, sourceUrl);
          });
      })
      .catch(function () {
        return {
          source: sourceUrl,
          objectUrl: false,
          cached: false
        };
      });
  }

  function releaseImage(image) {
    var source;

    if (!image) {
      return;
    }

    source = image._blazzingArtworkObjectUrl;
    image._blazzingArtworkObjectUrl = "";

    if (source &&
        global.URL &&
        typeof global.URL.revokeObjectURL === "function") {
      try {
        global.URL.revokeObjectURL(source);
      } catch (error) {
        // Revocation is best effort.
      }
    }
  }

  function releaseTree(root) {
    var images;
    var i;

    if (!root || typeof root.querySelectorAll !== "function") {
      return;
    }

    images = root.querySelectorAll("img");
    for (i = 0; i < images.length; i += 1) {
      releaseImage(images[i]);
    }
  }

  function apply(image, url) {
    var sourceUrl = String(url || "");
    var requestKey = cacheKey(sourceUrl);

    if (!image || !validUrl(sourceUrl)) {
      return Promise.resolve(false);
    }

    releaseImage(image);
    image._blazzingArtworkRequestKey = requestKey;

    return load(sourceUrl).then(function (result) {
      if (image._blazzingArtworkRequestKey !== requestKey) {
        if (result.objectUrl &&
            global.URL &&
            typeof global.URL.revokeObjectURL === "function") {
          global.URL.revokeObjectURL(result.source);
        }
        return false;
      }

      if (result.objectUrl) {
        image._blazzingArtworkObjectUrl = result.source;
      }

      image.src = result.source || sourceUrl;
      return true;
    });
  }

  function invalidate(url) {
    var sourceUrl = String(url || "");

    if (!validUrl(sourceUrl)) {
      return Promise.resolve();
    }

    return openDb()
      .then(function (db) {
        return deleteKeys(db, [cacheKey(sourceUrl)]);
      })
      .catch(function () {
        // Invalidating a cache entry is best effort.
      });
  }

  function clear() {
    return openDb().then(function (db) {
      return new Promise(function (resolve, reject) {
        var transaction;
        var request;

        try {
          transaction = db.transaction(STORE_NAME, "readwrite");
          request = transaction.objectStore(STORE_NAME).clear();
        } catch (error) {
          reject(error);
          return;
        }

        request.onerror = function () {
          reject(request.error || new Error("Falha ao limpar artwork."));
        };

        transaction.oncomplete = function () {
          resolve();
        };
      });
    });
  }

  global.BlazzingArtwork = {
    apply: apply,
    load: load,
    releaseImage: releaseImage,
    releaseTree: releaseTree,
    invalidate: invalidate,
    clear: clear,
    cacheKey: cacheKey,
    limits: {
      maxImageBytes: MAX_IMAGE_BYTES,
      maxCacheBytes: MAX_CACHE_BYTES,
      maxCacheItems: MAX_CACHE_ITEMS,
      maxAgeMs: MAX_AGE_MS
    }
  };
}(window));
