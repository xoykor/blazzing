/* SPDX-License-Identifier: MIT */
(function () {
    "use strict";

    var MAX_RESPONSE_BYTES = 128 * 1024 * 1024;
    var STORAGE_PREFIX = "blazzing.tizen.";
    var PLAYLIST_DIR = "playlists";
    var PLAYLIST_CHUNK_CHARS = 512 * 1024;
    var SECTION_WRITE_BUFFER_CHARS = 256 * 1024;

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

    function playlistSectionFileName(url, kind, summaryOnly) {
        var suffix = kind === "series" && summaryOnly ?
            "series-summary" : String(kind || "live");
        return "playlist-" + playlistHash(url) + "." + suffix + ".m3u8";
    }

    function playlistSectionKinds() {
        return [
            { kind: "live", summaryOnly: false },
            { kind: "vod", summaryOnly: false },
            { kind: "series", summaryOnly: false },
            { kind: "series", summaryOnly: true }
        ];
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

    function streamCachedPlaylistSection(url, kind, summaryOnly, onChunk) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");
        kind = String(kind || "live");

        if (!url) {
            return Promise.resolve(null);
        }
        if (typeof onChunk !== "function") {
            return Promise.reject(new Error("Callback de leitura da seção inválido."));
        }

        return resolvePlaylistDirectory("r").then(function (dir) {
            var file;

            if (!dir) {
                return null;
            }
            try {
                file = dir.resolve(playlistSectionFileName(url, kind, !!summaryOnly));
            } catch (missing) {
                return null;
            }

            return streamPlaylistFile(file, onChunk).then(function () {
                return {
                    url: url,
                    kind: kind,
                    summaryOnly: !!summaryOnly,
                    updatedAt: file.modified ? new Date(file.modified).getTime() : 0
                };
            });
        });
    }

    function removePlaylistSectionCachesInDir(dir, url) {
        var specs = playlistSectionKinds();

        return Promise.all(specs.map(function (spec) {
            var name = playlistSectionFileName(url, spec.kind, spec.summaryOnly);
            return removeFileByName(dir, name).then(function () {
                return removeFileByName(dir, name + ".tmp");
            });
        })).then(function () { return true; });
    }

    function clearPlaylistSectionCaches(url) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");
        if (!url) {
            return Promise.resolve();
        }

        return resolvePlaylistDirectory("rw").then(function (dir) {
            return removePlaylistSectionCachesInDir(dir, url);
        }).catch(function () {});
    }

    function buildPlaylistSectionCaches(url, classifyEntry, onProgress) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");

        if (!url) {
            return Promise.reject(new Error("URL M3U inválida."));
        }
        if (typeof classifyEntry !== "function") {
            return Promise.reject(new Error("Classificador M3U inválido."));
        }

        return resolvePlaylistDirectory("rw").then(function (dir) {
            var sourceFile;
            var specs = playlistSectionKinds();
            var files = {};
            var streams = {};
            var buffers = {
                live: "",
                vod: "",
                series: "",
                seriesSummary: ""
            };
            var seriesSeen = {};
            var rawStream = null;
            var carry = "";
            var pendingExtinf = "";
            var readChars = 0;

            try {
                sourceFile = dir.resolve(playlistFileName(url));
            } catch (missing) {
                throw new Error("Playlist salva não encontrada.");
            }

            function keyForSpec(spec) {
                return spec.kind === "series" && spec.summaryOnly ?
                    "seriesSummary" : spec.kind;
            }

            function appendBuffer(key, text) {
                buffers[key] += text;
                if (buffers[key].length >= SECTION_WRITE_BUFFER_CHARS) {
                    streams[key].write(buffers[key]);
                    buffers[key] = "";
                }
            }

            function appendHeader(line) {
                var text = line + "\n";
                appendBuffer("live", text);
                appendBuffer("vod", text);
                appendBuffer("series", text);
                appendBuffer("seriesSummary", text);
            }

            function processLine(rawLine) {
                var line = String(rawLine || "").replace(/\r$/, "");
                var classified;
                var entry;
                var key;

                if (!line) {
                    return;
                }

                if (line.indexOf("#EXTM3U") === 0 ||
                        line.indexOf("#EXT-X-LISTA-") === 0) {
                    appendHeader(line);
                    return;
                }

                if (line.indexOf("#EXTINF:") === 0) {
                    pendingExtinf = line;
                    return;
                }

                if (line.charAt(0) === "#") {
                    return;
                }

                if (!pendingExtinf) {
                    return;
                }

                classified = classifyEntry(url, pendingExtinf, line);
                entry = pendingExtinf + "\n" + line + "\n";
                pendingExtinf = "";

                if (!classified || !classified.kind) {
                    return;
                }

                if (classified.kind === "live") {
                    appendBuffer("live", entry);
                    return;
                }

                if (classified.kind === "vod") {
                    appendBuffer("vod", entry);
                    return;
                }

                if (classified.kind === "series") {
                    appendBuffer("series", entry);
                    key = classified.seriesKey || "";
                    if (key && !seriesSeen[key]) {
                        seriesSeen[key] = true;
                        appendBuffer("seriesSummary", entry);
                    }
                }
            }

            function consumeChunk(chunk) {
                var text = carry + String(chunk || "");
                var cursor = 0;
                var next;

                while (cursor < text.length) {
                    next = text.indexOf("\n", cursor);
                    if (next === -1) {
                        carry = text.slice(cursor);
                        return;
                    }
                    processLine(text.slice(cursor, next));
                    cursor = next + 1;
                }
                carry = "";
            }

            function closeAll() {
                if (rawStream) {
                    try { rawStream.close(); } catch (ignoreRawClose) {}
                    rawStream = null;
                }
                Object.keys(streams).forEach(function (key) {
                    try { streams[key].close(); } catch (ignoreClose) {}
                });
                streams = {};
            }

            function flushAll() {
                Object.keys(buffers).forEach(function (key) {
                    if (buffers[key]) {
                        streams[key].write(buffers[key]);
                        buffers[key] = "";
                    }
                });
            }

            function cleanupTemps() {
                return Promise.all(specs.map(function (spec) {
                    return removeFileByName(
                        dir,
                        playlistSectionFileName(url, spec.kind, spec.summaryOnly) + ".tmp"
                    );
                }));
            }

            function publishAll() {
                var index = 0;

                function next() {
                    var spec;
                    var tmp;
                    var finalName;

                    if (index >= specs.length) {
                        return Promise.resolve(true);
                    }

                    spec = specs[index];
                    index += 1;
                    finalName = playlistSectionFileName(url, spec.kind, spec.summaryOnly);

                    try {
                        tmp = dir.resolve(finalName + ".tmp");
                    } catch (error) {
                        return Promise.reject(storageError(
                            "Cache de seção temporário ausente",
                            error
                        ));
                    }

                    return new Promise(function (resolve, reject) {
                        try {
                            dir.moveTo(
                                tmp.fullPath,
                                "wgt-private/" + PLAYLIST_DIR + "/" + finalName,
                                true,
                                function () { resolve(); },
                                function (error) {
                                    reject(storageError(
                                        "Falha ao publicar cache de seção",
                                        error
                                    ));
                                }
                            );
                        } catch (error) {
                            reject(storageError(
                                "Falha ao substituir cache de seção",
                                error
                            ));
                        }
                    }).then(next);
                }

                return next();
            }

            function openOutputFiles() {
                return cleanupTemps().then(function () {
                    return Promise.all(specs.map(function (spec) {
                        var finalName = playlistSectionFileName(
                            url,
                            spec.kind,
                            spec.summaryOnly
                        );
                        var tmpName = finalName + ".tmp";
                        var file;
                        var key = keyForSpec(spec);

                        try {
                            file = dir.createFile(tmpName);
                        } catch (exists) {
                            file = dir.resolve(tmpName);
                        }
                        files[key] = file;

                        return new Promise(function (resolve, reject) {
                            file.openStream(
                                "w",
                                function (stream) {
                                    streams[key] = stream;
                                    resolve();
                                },
                                function (error) {
                                    reject(storageError(
                                        "Falha ao abrir cache de seção",
                                        error
                                    ));
                                },
                                "UTF-8"
                            );
                        });
                    }));
                });
            }

            function scanSource() {
                return new Promise(function (resolve, reject) {
                    sourceFile.openStream(
                        "r",
                        function (stream) {
                            rawStream = stream;

                            function pump() {
                                var chunk;

                                try {
                                    if (stream.bytesAvailable <= 0) {
                                        if (carry) {
                                            processLine(carry);
                                            carry = "";
                                        }
                                        flushAll();
                                        closeAll();
                                        publishAll().then(resolve).catch(reject);
                                        return;
                                    }

                                    chunk = stream.read(
                                        Math.min(
                                            stream.bytesAvailable,
                                            PLAYLIST_CHUNK_CHARS
                                        )
                                    );
                                    readChars += chunk.length;
                                    consumeChunk(chunk);

                                    if (typeof onProgress === "function") {
                                        onProgress(readChars);
                                    }
                                } catch (error) {
                                    closeAll();
                                    cleanupTemps();
                                    reject(storageError(
                                        "Falha ao indexar playlist",
                                        error
                                    ));
                                    return;
                                }

                                setTimeout(pump, 0);
                            }

                            try {
                                stream.position = 0;
                                setTimeout(pump, 0);
                            } catch (error) {
                                closeAll();
                                cleanupTemps();
                                reject(storageError(
                                    "Falha ao iniciar indexação da playlist",
                                    error
                                ));
                            }
                        },
                        function (error) {
                            closeAll();
                            cleanupTemps();
                            reject(storageError(
                                "Falha ao abrir playlist para indexação",
                                error
                            ));
                        },
                        "UTF-8"
                    );
                });
            }

            return openOutputFiles().then(scanSource).catch(function (error) {
                closeAll();
                return cleanupTemps().then(function () {
                    throw error;
                });
            });
        });
    }

    function cachedPlaylistExists(url) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");
        if (!url) {
            return Promise.resolve(false);
        }

        return resolvePlaylistDirectory("r").then(function (dir) {
            if (!dir) {
                return false;
            }

            try {
                dir.resolve(playlistFileName(url));
                return true;
            } catch (missing) {
                return false;
            }
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
                            function () {
                                removePlaylistSectionCachesInDir(dir, url)
                                    .then(function () { resolve(true); });
                            },
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

    function downloadApiAvailable() {
        return !!(
            window.tizen &&
            window.tizen.download &&
            typeof window.tizen.DownloadRequest === "function"
        );
    }

    function removeFileByName(dir, name) {
        return new Promise(function (resolve) {
            var file;

            try {
                file = dir.resolve(name);
            } catch (missing) {
                resolve();
                return;
            }

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
    }

    function publishDownloadedPlaylist(dir, tmpName, finalName) {
        return new Promise(function (resolve, reject) {
            var tmp;

            try {
                tmp = dir.resolve(tmpName);
            } catch (error) {
                reject(storageError("Download concluído sem arquivo temporário", error));
                return;
            }

            tmp.openStream(
                "r",
                function (stream) {
                    var sample = "";
                    var sampleSize;

                    try {
                        stream.position = 0;
                        sampleSize = Math.min(stream.bytesAvailable, 128 * 1024);
                        sample = sampleSize > 0 ? stream.read(sampleSize) : "";
                        stream.close();
                    } catch (error) {
                        try { stream.close(); } catch (ignoreClose) {}
                        reject(storageError("Falha ao validar playlist baixada", error));
                        return;
                    }

                    if (sample.indexOf("#EXTM3U") === -1 &&
                            sample.indexOf("#EXTINF:") === -1) {
                        removeFileByName(dir, tmpName).then(function () {
                            reject(new Error(
                                "O conteúdo recebido não parece ser uma playlist M3U."
                            ));
                        });
                        return;
                    }

                    try {
                        dir.moveTo(
                            tmp.fullPath,
                            "wgt-private/" + PLAYLIST_DIR + "/" + finalName,
                            true,
                            function () { resolve(true); },
                            function (error) {
                                reject(storageError(
                                    "Falha ao publicar playlist baixada",
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
                },
                function (error) {
                    reject(storageError("Falha ao abrir playlist baixada", error));
                },
                "UTF-8"
            );
        });
    }

    function downloadPlaylistToCache(url, options) {
        options = options || {};
        url = String(url || "").replace(/^\s+|\s+$/g, "");

        if (!url || !/^https?:\/\//i.test(url)) {
            return Promise.reject(new Error("URL M3U inválida."));
        }
        if (!downloadApiAvailable()) {
            return Promise.reject(new Error(
                "Download direto do Tizen indisponível neste dispositivo."
            ));
        }

        return resolvePlaylistDirectory("rw").then(function (dir) {
            var finalName = playlistFileName(url);
            var tmpName = finalName + ".download";
            var limit = options.maxBytes || MAX_RESPONSE_BYTES;
            var timeout = options.timeout || 180000;

            return removeFileByName(dir, tmpName).then(function () {
                return new Promise(function (resolve, reject) {
                    var request;
                    var downloadId = -1;
                    var settled = false;
                    var timer = 0;

                    function cleanupTemp() {
                        removeFileByName(dir, tmpName);
                    }

                    function fail(message) {
                        if (settled) { return; }
                        settled = true;
                        clearTimeout(timer);
                        cleanupTemp();
                        reject(new Error(message));
                    }

                    function cancelForLimit() {
                        try {
                            if (downloadId >= 0) {
                                window.tizen.download.cancel(downloadId);
                            }
                        } catch (ignoreCancel) {}
                        fail("A resposta excede o limite de 128 MiB.");
                    }

                    try {
                        request = new window.tizen.DownloadRequest(
                            url,
                            "wgt-private/" + PLAYLIST_DIR,
                            tmpName
                        );

                        downloadId = window.tizen.download.start(request, {
                            onprogress: function (id, receivedSize, totalSize) {
                                if (receivedSize > limit ||
                                        (totalSize > 0 && totalSize > limit)) {
                                    cancelForLimit();
                                }
                                if (typeof options.onProgress === "function") {
                                    options.onProgress(receivedSize, totalSize);
                                }
                            },
                            onpaused: function () {},
                            oncanceled: function () {
                                if (!settled) {
                                    fail("Download da playlist cancelado.");
                                }
                            },
                            oncompleted: function () {
                                if (settled) { return; }
                                clearTimeout(timer);

                                publishDownloadedPlaylist(
                                    dir,
                                    tmpName,
                                    finalName
                                ).then(function () {
                                    return removePlaylistSectionCachesInDir(dir, url);
                                }).then(function () {
                                    if (settled) { return; }
                                    settled = true;
                                    resolve(true);
                                }).catch(function (error) {
                                    fail(error.message || "Falha ao salvar playlist baixada.");
                                });
                            },
                            onfailed: function (id, error) {
                                var failure = error || id;
                                fail(
                                    "Falha no download da playlist" +
                                    (failure && failure.name ?
                                        " (" + failure.name + ")" : "") +
                                    "."
                                );
                            }
                        });

                        if (downloadId < 0) {
                            fail("A TV não conseguiu iniciar o download da playlist.");
                            return;
                        }

                        timer = setTimeout(function () {
                            try {
                                window.tizen.download.cancel(downloadId);
                            } catch (ignoreTimeoutCancel) {}
                            fail("Tempo limite excedido ao baixar a playlist.");
                        }, timeout);
                    } catch (error) {
                        fail(
                            "Não foi possível iniciar o Download API do Tizen" +
                            (error && error.name ? " (" + error.name + ")" : "") +
                            "."
                        );
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
            }).then(function () {
                return removePlaylistSectionCachesInDir(dir, url);
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
        cachedPlaylistExists: cachedPlaylistExists,
        streamCachedPlaylist: streamCachedPlaylist,
        streamCachedPlaylistSection: streamCachedPlaylistSection,
        buildPlaylistSectionCaches: buildPlaylistSectionCaches,
        clearPlaylistSectionCaches: clearPlaylistSectionCaches,
        saveCachedPlaylist: saveCachedPlaylist,
        downloadPlaylistToCache: downloadPlaylistToCache,
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
