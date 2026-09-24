/* SPDX-License-Identifier: MIT */
(function () {
    "use strict";

    var MAX_RESPONSE_BYTES = 128 * 1024 * 1024;
    var STORAGE_PREFIX = "blazzing.tizen.";
    var PLAYLIST_DIR = "playlists";
    var PLAYLIST_CHUNK_CHARS = 512 * 1024;

    function storageError(prefix, error) {
        return new Error(
            prefix +
            (error && error.name ? " (" + error.name + ")" : "") +
            (error && error.message ? ": " + error.message : ".")
        );
    }

    function playlistHash(value) {
        var text = String(value || "");
        var h1 = 2166136261;
        var h2 = 2246822519;
        var i;

        for (i = 0; i < text.length; i += 1) {
            h1 ^= text.charCodeAt(i);
            h1 = Math.imul ? Math.imul(h1, 16777619) : ((h1 * 16777619) >>> 0);
            h2 ^= text.charCodeAt(text.length - 1 - i);
            h2 = Math.imul ? Math.imul(h2, 3266489917) : ((h2 * 3266489917) >>> 0);
        }

        return ("00000000" + (h1 >>> 0).toString(16)).slice(-8) +
            ("00000000" + (h2 >>> 0).toString(16)).slice(-8);
    }

    function playlistFileName(url) {
        return "playlist-" + playlistHash(url) + ".m3u8";
    }

    function filesystemAvailable() {
        return !!(window.tizen && window.tizen.filesystem);
    }

    function resolvePlaylistDirectory(mode) {
        return new Promise(function (resolve, reject) {
            if (!filesystemAvailable()) {
                reject(new Error("Filesystem persistente do Tizen indisponível."));
                return;
            }

            window.tizen.filesystem.resolve(
                "wgt-private",
                function (root) {
                    var dir;
                    try {
                        dir = root.resolve(PLAYLIST_DIR);
                    } catch (missing) {
                        if (mode !== "rw") {
                            resolve(null);
                            return;
                        }
                        try {
                            dir = root.createDirectory(PLAYLIST_DIR);
                        } catch (createError) {
                            reject(storageError(
                                "Falha ao criar diretório privado de playlists",
                                createError
                            ));
                            return;
                        }
                    }
                    resolve(dir);
                },
                function (error) {
                    reject(storageError(
                        "Falha ao acessar armazenamento privado da TV",
                        error
                    ));
                },
                mode || "r"
            );
        });
    }

    function readPlaylistFile(file) {
        return new Promise(function (resolve, reject) {
            file.openStream(
                "r",
                function (stream) {
                    var parts = [];
                    try {
                        stream.position = 0;
                        while (stream.bytesAvailable > 0) {
                            parts.push(stream.read(
                                Math.min(stream.bytesAvailable, PLAYLIST_CHUNK_CHARS)
                            ));
                        }
                        stream.close();
                        resolve(parts.join(""));
                    } catch (error) {
                        try { stream.close(); } catch (ignoreClose) {}
                        reject(storageError("Falha ao ler playlist salva", error));
                    }
                },
                function (error) {
                    reject(storageError("Falha ao abrir playlist salva", error));
                },
                "UTF-8"
            );
        });
    }

    function streamPlaylistFile(file, onChunk) {
        return new Promise(function (resolve, reject) {
            file.openStream(
                "r",
                function (stream) {
                    var closed = false;

                    function closeQuietly() {
                        if (closed) { return; }
                        closed = true;
                        try { stream.close(); } catch (ignoreClose) {}
                    }

                    function pump() {
                        var chunk;

                        try {
                            if (stream.bytesAvailable <= 0) {
                                closeQuietly();
                                resolve(true);
                                return;
                            }

                            chunk = stream.read(
                                Math.min(stream.bytesAvailable, PLAYLIST_CHUNK_CHARS)
                            );
                            onChunk(chunk);
                        } catch (error) {
                            closeQuietly();
                            reject(storageError("Falha ao ler playlist salva", error));
                            return;
                        }

                        /*
                         * Cede a thread principal entre blocos. TVs Tizen mais
                         * antigas podem encerrar o Web App quando uma playlist
                         * grande monopoliza o event loop durante o boot.
                         */
                        setTimeout(pump, 0);
                    }

                    try {
                        stream.position = 0;
                        setTimeout(pump, 0);
                    } catch (error) {
                        closeQuietly();
                        reject(storageError("Falha ao preparar leitura da playlist", error));
                    }
                },
                function (error) {
                    reject(storageError("Falha ao abrir playlist salva", error));
                },
                "UTF-8"
            );
        });
    }

    function streamCachedPlaylist(url, onChunk) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");
        if (!url) {
            return Promise.resolve(null);
        }
        if (typeof onChunk !== "function") {
            return Promise.reject(new Error("Callback de leitura da playlist inválido."));
        }

        return resolvePlaylistDirectory("r").then(function (dir) {
            var file;

            if (!dir) {
                return null;
            }
            try {
                file = dir.resolve(playlistFileName(url));
            } catch (missing) {
                return null;
            }

            return streamPlaylistFile(file, onChunk).then(function () {
                return {
                    url: url,
                    updatedAt: file.modified ? new Date(file.modified).getTime() : 0
                };
            });
        });
    }

    function writePlaylistFile(file, text) {
        return new Promise(function (resolve, reject) {
            file.openStream(
                "w",
                function (stream) {
                    var offset = 0;
                    try {
                        while (offset < text.length) {
                            stream.write(text.slice(
                                offset,
                                offset + PLAYLIST_CHUNK_CHARS
                            ));
                            offset += PLAYLIST_CHUNK_CHARS;
                        }
                        stream.close();
                        resolve(true);
                    } catch (error) {
                        try { stream.close(); } catch (ignoreClose) {}
                        reject(storageError("Falha ao gravar playlist na TV", error));
                    }
                },
                function (error) {
                    reject(storageError("Falha ao abrir arquivo de playlist", error));
                },
                "UTF-8"
            );
        });
    }

    function cachedPlaylist(url) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");
        if (!url) {
            return Promise.resolve(null);
        }

        return resolvePlaylistDirectory("r").then(function (dir) {
            var file;
            if (!dir) {
                return null;
            }
            try {
                file = dir.resolve(playlistFileName(url));
            } catch (missing) {
                return null;
            }
            return readPlaylistFile(file).then(function (text) {
                return {
                    url: url,
                    text: text,
                    updatedAt: file.modified ? new Date(file.modified).getTime() : 0
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

        return resolvePlaylistDirectory("rw").then(function (dir) {
            var name = playlistFileName(url);
            var tmpName = name + ".tmp";
            var tmp;

            try {
                try {
                    tmp = dir.resolve(tmpName);
                } catch (missingTmp) {
                    tmp = dir.createFile(tmpName);
                }
            } catch (error) {
                throw storageError("Falha ao preparar arquivo temporário", error);
            }

            return writePlaylistFile(tmp, text).then(function () {
                return new Promise(function (resolve, reject) {
                    /*
                     * Publica a atualização só depois da escrita completa.
                     * moveTo substitui o arquivo antigo sem exigir uma segunda
                     * cópia integral de ~50 MiB.
                     */
                    try {
                        dir.moveTo(
                            tmp.fullPath,
                            "wgt-private/" + PLAYLIST_DIR + "/" + name,
                            true,
                            function () { resolve(true); },
                            function (error) {
                                reject(storageError(
                                    "Falha ao publicar playlist salva",
                                    error
                                ));
                            }
                        );
                    } catch (error) {
                        reject(storageError(
                            "Falha ao substituir playlist salva",
                            error
                        ));
                    }
                });
            });
        });
    }

    function deleteCachedPlaylist(url) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");
        if (!url) {
            return Promise.resolve();
        }

        return resolvePlaylistDirectory("rw").then(function (dir) {
            var file;
            if (!dir) {
                return;
            }
            try {
                file = dir.resolve(playlistFileName(url));
            } catch (missing) {
                return;
            }

            return new Promise(function (resolve) {
                try {
                    dir.deleteFile(
                        file.fullPath,
                        function () { resolve(); },
                        function () { resolve(); }
                    );
                } catch (error) {
                    resolve();
                }
            });
        }).catch(function () {
            /* Excluir o perfil continua possível mesmo sem acesso ao arquivo. */
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

    function postJson(url, value, options) {
        options = options || {};
        return new Promise(function (resolve, reject) {
            var xhr = new XMLHttpRequest();
            var done = false;
            var limit = options.maxBytes || (2 * 1024 * 1024);
            var body;

            function fail(message) {
                if (done) { return; }
                done = true;
                reject(new Error(message));
            }

            try {
                body = JSON.stringify(value);
                xhr.open("POST", url, true);
                xhr.timeout = options.timeout || 15000;
                xhr.withCredentials = false;
                xhr.setRequestHeader("Content-Type", "application/json");
                xhr.onprogress = function (event) {
                    if (event.loaded > limit) {
                        try { xhr.abort(); } catch (ignore) {}
                        fail("A resposta excede o limite permitido.");
                    }
                };
                xhr.onerror = function () { fail("Falha de rede ao acessar o servidor."); };
                xhr.ontimeout = function () { fail("Tempo limite excedido ao acessar o servidor."); };
                xhr.onabort = function () { fail("Requisição cancelada."); };
                xhr.onload = function () {
                    var parsed;
                    if (done) { return; }
                    if (xhr.status < 200 || xhr.status >= 300) {
                        fail("Servidor respondeu HTTP " + xhr.status + ".");
                        return;
                    }
                    if ((xhr.responseText || "").length > limit) {
                        fail("A resposta excede o limite permitido.");
                        return;
                    }
                    try {
                        parsed = JSON.parse(xhr.responseText || "{}");
                    } catch (error) {
                        fail("O servidor retornou JSON inválido.");
                        return;
                    }
                    done = true;
                    resolve(parsed);
                };
                xhr.send(body);
            } catch (error) {
                fail("Não foi possível iniciar a requisição.");
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
        streamCachedPlaylist: streamCachedPlaylist,
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
        json: requestJson,
        postJson: postJson
    };
    window.BlazzingStorage = storage;
}());
