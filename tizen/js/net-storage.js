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
            writeJson("profiles", storage.profiles().filter(function (profile) {
                return profile.id !== id;
            }));
        },
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
