/* SPDX-License-Identifier: MIT */
(function () {
    "use strict";

    var MAX_RESPONSE_BYTES = 128 * 1024 * 1024;
    var STORAGE_PREFIX = "blazzing.tizen.";

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

    /*
     * Samsung TV Web Storage is limited to roughly 5 MB. Playlists can be much
     * larger, so keep their raw M3U text in the application's private
     * persistent filesystem. "wgt-private" survives app termination and TV
     * power cycles, but is removed when the application is uninstalled.
     */
    function playlistCacheName(url) {
        var hash = 2166136261;
        var text = String(url || "");
        var i;

        for (i = 0; i < text.length; i += 1) {
            hash ^= text.charCodeAt(i);
            hash = (hash * 16777619) >>> 0;
        }

        return "playlist-" +
            ("00000000" + hash.toString(16)).slice(-8) +
            ".m3u";
    }

    function hasPersistentFilesystem() {
        return !!(
            window.tizen &&
            window.tizen.filesystem &&
            window.tizen.filesystem.resolve
        );
    }

    function readM3uCache(url) {
        return new Promise(function (resolve) {
            var fileName = playlistCacheName(url);

            if (!hasPersistentFilesystem()) {
                resolve("");
                return;
            }

            try {
                window.tizen.filesystem.resolve(
                    "wgt-private",
                    function (dir) {
                        var file;

                        try {
                            file = dir.resolve(fileName);
                        } catch (error) {
                            resolve("");
                            return;
                        }

                        file.readAsText(
                            function (text) {
                                resolve(text || "");
                            },
                            function () {
                                resolve("");
                            },
                            "UTF-8"
                        );
                    },
                    function () {
                        resolve("");
                    },
                    "r"
                );
            } catch (error) {
                resolve("");
            }
        });
    }

    function writeM3uCache(url, text) {
        return new Promise(function (resolve) {
            var fileName = playlistCacheName(url);

            if (!hasPersistentFilesystem() || !text) {
                resolve(false);
                return;
            }

            try {
                window.tizen.filesystem.resolve(
                    "wgt-private",
                    function (dir) {
                        var file;

                        try {
                            file = dir.resolve(fileName);
                        } catch (resolveError) {
                            try {
                                file = dir.createFile(fileName);
                            } catch (createError) {
                                resolve(false);
                                return;
                            }
                        }

                        file.openStream(
                            "w",
                            function (stream) {
                                try {
                                    stream.write(text);
                                    stream.close();
                                    resolve(true);
                                } catch (writeError) {
                                    try { stream.close(); } catch (ignoreClose) {}
                                    resolve(false);
                                }
                            },
                            function () {
                                resolve(false);
                            },
                            "UTF-8"
                        );
                    },
                    function () {
                        resolve(false);
                    },
                    "rw"
                );
            } catch (error) {
                resolve(false);
            }
        });
    }

    function removeM3uCache(url) {
        return new Promise(function (resolve) {
            var fileName = playlistCacheName(url);

            if (!hasPersistentFilesystem()) {
                resolve(false);
                return;
            }

            try {
                window.tizen.filesystem.resolve(
                    "wgt-private",
                    function (dir) {
                        var file;

                        try {
                            file = dir.resolve(fileName);
                        } catch (error) {
                            resolve(true);
                            return;
                        }

                        dir.deleteFile(
                            file.fullPath,
                            function () { resolve(true); },
                            function () { resolve(false); }
                        );
                    },
                    function () { resolve(false); },
                    "rw"
                );
            } catch (error) {
                resolve(false);
            }
        });
    }

    var storage = {
        profiles: function () {
            return readJson("profiles", []);
        },
        saveProfile: function (profile) {
            var profiles = storage.profiles();
            var copy = JSON.parse(JSON.stringify(profile));
            var i;
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
            var profiles = storage.profiles();
            var removed = null;

            writeJson("profiles", profiles.filter(function (profile) {
                if (profile.id === id) {
                    removed = profile;
                    return false;
                }
                return true;
            }));

            if (removed && removed.type === "m3u" && removed.url) {
                removeM3uCache(removed.url);
            }
        },
        readM3uCache: readM3uCache,
        writeM3uCache: writeM3uCache,
        removeM3uCache: removeM3uCache,
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
