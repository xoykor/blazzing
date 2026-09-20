/* SPDX-License-Identifier: MIT */
(function () {
    "use strict";

    var MAX_RESPONSE_BYTES = 128 * 1024 * 1024;
    var STORAGE_PREFIX = "blazzing.tizen.";
    var PLAYLIST_DB_NAME = "blazzing-tizen-playlists";
    var PLAYLIST_DB_VERSION = 1;
    var PLAYLIST_STORE = "playlists";
    var playlistDbPromise = null;

    function indexedDbFactory() {
        return window.indexedDB || window.webkitIndexedDB || window.mozIndexedDB || null;
    }

    function openPlaylistDb() {
        var factory = indexedDbFactory();

        if (playlistDbPromise) {
            return playlistDbPromise;
        }
        if (!factory) {
            return Promise.reject(new Error("Armazenamento persistente de playlists indisponível."));
        }

        playlistDbPromise = new Promise(function (resolve, reject) {
            var request;
            try {
                request = factory.open(PLAYLIST_DB_NAME, PLAYLIST_DB_VERSION);
            } catch (error) {
                reject(error);
                return;
            }

            request.onupgradeneeded = function () {
                var db = request.result;
                if (!db.objectStoreNames.contains(PLAYLIST_STORE)) {
                    db.createObjectStore(PLAYLIST_STORE, { keyPath: "url" });
                }
            };
            request.onsuccess = function () { resolve(request.result); };
            request.onerror = function () {
                reject(request.error || new Error("Falha ao abrir cache de playlists."));
            };
        });

        return playlistDbPromise;
    }

    function cachedPlaylist(url) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");
        if (!url) {
            return Promise.resolve(null);
        }

        return openPlaylistDb().then(function (db) {
            return new Promise(function (resolve, reject) {
                var tx = db.transaction(PLAYLIST_STORE, "readonly");
                var request = tx.objectStore(PLAYLIST_STORE).get(url);

                request.onsuccess = function () {
                    var row = request.result;
                    resolve(row && typeof row.text === "string" ? row : null);
                };
                request.onerror = function () {
                    reject(request.error || new Error("Falha ao ler playlist salva."));
                };
            });
        });
    }

    function saveCachedPlaylist(url, text) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");
        text = String(text || "");

        if (!url || !text) {
            return Promise.reject(new Error("Playlist inválida para armazenamento."));
        }

        return openPlaylistDb().then(function (db) {
            return new Promise(function (resolve, reject) {
                var tx = db.transaction(PLAYLIST_STORE, "readwrite");
                tx.objectStore(PLAYLIST_STORE).put({
                    url: url,
                    text: text,
                    updatedAt: Date.now()
                });
                tx.oncomplete = function () { resolve(true); };
                tx.onerror = function () {
                    reject(tx.error || new Error("Falha ao salvar playlist na TV."));
                };
                tx.onabort = function () {
                    reject(tx.error || new Error("Armazenamento da playlist foi cancelado."));
                };
            });
        });
    }

    function deleteCachedPlaylist(url) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");
        if (!url) {
            return Promise.resolve();
        }

        return openPlaylistDb().then(function (db) {
            return new Promise(function (resolve, reject) {
                var tx = db.transaction(PLAYLIST_STORE, "readwrite");
                tx.objectStore(PLAYLIST_STORE).delete(url);
                tx.oncomplete = function () { resolve(); };
                tx.onerror = function () {
                    reject(tx.error || new Error("Falha ao remover playlist salva."));
                };
            });
        }).catch(function () {
            /* Removing a profile should still work if IndexedDB is unavailable. */
        });
    }


    function requestText(url, options) {
        options = options || {};
        return new Promise(function (resolve, reject) {
            var xhr = new XMLHttpRequest();
            var done = false;
            var limit = options.maxBytes || MAX_RESPONSE_BYTES;

            function fail(message) {
                if (done) { return; }
                done = true;
                reject(new Error(message));
            }

            try {
                xhr.open("GET", url, true);
                xhr.timeout = options.timeout || 60000;
                xhr.onprogress = function (event) {
                    if (event.loaded > limit) {
                        try { xhr.abort(); } catch (ignore) {}
                        fail("A resposta excede o limite de 128 MiB.");
                    }
                };
                xhr.onerror = function () { fail("Falha de rede ao acessar o servidor."); };
                xhr.ontimeout = function () { fail("Tempo limite excedido ao acessar o servidor."); };
                xhr.onabort = function () { fail("Requisição cancelada."); };
                xhr.onload = function () {
                    var lengthHeader;
                    var declaredLength;
                    if (done) { return; }
                    if (xhr.status < 200 || xhr.status >= 300) {
                        fail("Servidor respondeu HTTP " + xhr.status + ".");
                        return;
                    }
                    lengthHeader = xhr.getResponseHeader("Content-Length");
                    declaredLength = lengthHeader ? parseInt(lengthHeader, 10) : 0;
                    if (declaredLength > limit || (xhr.responseText && xhr.responseText.length > limit)) {
                        fail("A resposta excede o limite de 128 MiB.");
                        return;
                    }
                    done = true;
                    resolve(xhr.responseText || "");
                };
                xhr.send();
            } catch (error) {
                fail("Não foi possível iniciar a requisição.");
            }
        });
    }

    function requestJson(url, options) {
        return requestText(url, options).then(function (text) {
            try {
                return JSON.parse(text);
            } catch (error) {
                throw new Error("O servidor retornou JSON inválido.");
            }
        });
    }

    function readJson(key, fallback) {
        try {
            var raw = localStorage.getItem(STORAGE_PREFIX + key);
            return raw ? JSON.parse(raw) : fallback;
        } catch (error) {
            return fallback;
        }
    }

    function writeJson(key, value) {
        localStorage.setItem(STORAGE_PREFIX + key, JSON.stringify(value));
    }

    var storage = {
        profiles: function () {
            return readJson("profiles", []);
        },
        saveProfile: function (profile) {
            var profiles = storage.profiles();
            var copy = JSON.parse(JSON.stringify(profile));
            var normalizedUrl = String(copy.url || "").replace(/^\s+|\s+$/g, "");
            var i;

            /*
             * A manually entered/QR M3U profile has no id yet. Reuse an
             * existing profile with the same URL instead of creating another
             * card every time the playlist is opened.
             */
            if (!copy.id && copy.type === "m3u" && normalizedUrl) {
                for (i = 0; i < profiles.length; i += 1) {
                    if (profiles[i].type === "m3u" &&
                            String(profiles[i].url || "").replace(/^\s+|\s+$/g, "") ===
                                normalizedUrl) {
                        copy.id = profiles[i].id;
                        break;
                    }
                }
            }

            copy.id = copy.id || ("profile-" + Date.now());

            for (i = 0; i < profiles.length; i += 1) {
                if (profiles[i].id === copy.id) {
                    profiles[i] = copy;
                    writeJson("profiles", profiles);
                    return copy;
                }
            }
            profiles.unshift(copy);
            writeJson("profiles", profiles.slice(0, 30));
            return copy;
        },
        removeProfile: function (id) {
            var removed = null;
            var profiles = storage.profiles().filter(function (profile) {
                if (profile.id === id) {
                    removed = profile;
                    return false;
                }
                return true;
            });
            writeJson("profiles", profiles);

            if (removed && removed.type === "m3u" && removed.url) {
                deleteCachedPlaylist(removed.url);
            }
            if (storage.lastOpenedProfileId() === id) {
                storage.setLastOpenedProfileId("");
            }
        },
        lastOpenedProfileId: function () {
            return String(readJson("lastOpenedProfileId", "") || "");
        },
        setLastOpenedProfileId: function (id) {
            writeJson("lastOpenedProfileId", String(id || ""));
        },
        cachedPlaylist: cachedPlaylist,
        saveCachedPlaylist: saveCachedPlaylist,
        deleteCachedPlaylist: deleteCachedPlaylist,
        favorites: function () {
            return readJson("favorites", {});
        },
        isFavorite: function (uid) {
            return !!storage.favorites()[uid];
        },
        toggleFavorite: function (uid) {
            var favorites = storage.favorites();
            if (favorites[uid]) { delete favorites[uid]; }
            else { favorites[uid] = true; }
            writeJson("favorites", favorites);
            return !!favorites[uid];
        },
        getProgress: function (uid) {
            return readJson("progress", {})[uid] || 0;
        },
        setProgress: function (uid, milliseconds) {
            var progress = readJson("progress", {});
            progress[uid] = Math.max(0, Math.floor(milliseconds || 0));
            writeJson("progress", progress);
        }
    };

    window.BlazzingNet = {
        MAX_RESPONSE_BYTES: MAX_RESPONSE_BYTES,
        text: requestText,
        json: requestJson
    };
    window.BlazzingStorage = storage;
}());
