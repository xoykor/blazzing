/* SPDX-License-Identifier: GPL-3.0-only */
/* Windows bridge layered over the shared Tizen/browser frontend. */
(function () {
    "use strict";

    var native = window.BlazzingWindowsNative;
    var originalStorage = window.BlazzingStorage;
    var currentItem = null;
    var currentTimeMs = 0;
    var currentSession = 0;
    var fallbackRows = null;
    var fallbackCursor = 0;
    var fallbackPromise = null;
    var fallbackBusy = false;
    var generation = 0;
    var onState = function () {};
    var onTime = function () {};

    if (!native) {
        return;
    }

    if (window.BlazzingNet) {
        window.BlazzingNet.text = function (url, options) {
            return native.netText(url, options || {});
        };
        window.BlazzingNet.json = function (url, options) {
            return native.netJson(url, options || {});
        };
    }

    if (originalStorage) {
        originalStorage.cachedPlaylist = function (url) {
            return native.cachedPlaylist(url);
        };
        originalStorage.saveCachedPlaylist = function (url, text) {
            return native.saveCachedPlaylist(url, text);
        };
        originalStorage.deleteCachedPlaylist = function (url) {
            return native.deleteCachedPlaylist(url);
        };
    }

    function fallbackShardUrl(item) {
        var id = String(item && item.fallbackId || "");
        var base = String(item && item.fallbackIndexBase || "").replace(/\/+$/, "");
        var shardLength = parseInt(item && item.fallbackIndexShardLength, 10) || 2;
        var version = String(item && item.fallbackIndexVersion || "");

        if (!id || !base || !/^https?:\/\//i.test(base)) {
            return "";
        }
        shardLength = Math.max(1, Math.min(4, shardLength));
        return base + "/" + id.slice(0, shardLength) + ".json" +
            (version ? "?v=" + encodeURIComponent(version) : "");
    }

    function loadFallbackRows(item) {
        var shardUrl;
        var id;

        if (fallbackRows) {
            return Promise.resolve(fallbackRows);
        }
        if (fallbackPromise) {
            return fallbackPromise;
        }

        shardUrl = fallbackShardUrl(item);
        id = String(item && item.fallbackId || "");
        if (!shardUrl || !id || !window.BlazzingNet || !window.BlazzingNet.json) {
            return Promise.resolve([]);
        }

        fallbackPromise = window.BlazzingNet.json(shardUrl, {
            timeout: 10000,
            maxBytes: 4 * 1024 * 1024
        }).then(function (payload) {
            var rows = payload && Array.isArray(payload[id]) ? payload[id] : [];
            var seen = {};
            fallbackRows = [];
            rows.forEach(function (row) {
                var url;
                if (!Array.isArray(row) || !row.length) { return; }
                url = String(row[0] || "");
                if (!/^https?:\/\//i.test(url) || /workers\.dev/i.test(url)) { return; }
                if (url === String(item.url || "") || seen[url]) { return; }
                seen[url] = true;
                fallbackRows.push({
                    url: url,
                    referer: String(row[2] || ""),
                    userAgent: String(row[3] || "")
                });
            });
            return fallbackRows;
        }).catch(function () {
            fallbackRows = [];
            return fallbackRows;
        });
        return fallbackPromise;
    }

    function openNative(item, resumeMs, source) {
        var myGeneration = generation;
        source = source || {
            url: item.url,
            referer: item.referer || "",
            userAgent: item.userAgent || ""
        };

        onState("Preparando…");
        return native.playerOpen({
            url: source.url,
            referer: source.referer || "",
            userAgent: source.userAgent || "",
            resumeMs: Math.max(0, Number(resumeMs) || 0),
            kind: item.kind || ""
        }).then(function (result) {
            if (myGeneration === generation && result) {
                currentSession = Number(result.sessionId) || 0;
            }
        }).catch(function (error) {
            if (myGeneration === generation) {
                tryNextFallback(item, resumeMs, error && error.message ?
                    error.message : "Falha ao iniciar o mpv.");
            }
        });
    }

    function tryNextFallback(item, resumeMs, reason) {
        var myGeneration = generation;
        if (!item || fallbackBusy) { return; }
        fallbackBusy = true;

        loadFallbackRows(item).then(function (rows) {
            var row;
            if (myGeneration !== generation || currentItem !== item) {
                return;
            }
            while (fallbackCursor < rows.length) {
                row = rows[fallbackCursor++];
                if (row && row.url) {
                    fallbackBusy = false;
                    onState("Fonte indisponível; tentando alternativa " +
                        fallbackCursor + "…");
                    openNative(item, resumeMs || currentTimeMs, row);
                    return;
                }
            }
            fallbackBusy = false;
            onState(reason || "Não foi possível reproduzir nenhuma fonte disponível.");
        });
    }

    function open(item, resumeMs) {
        generation += 1;
        currentItem = item;
        currentTimeMs = Math.max(0, Number(resumeMs) || 0);
        currentSession = 0;
        fallbackRows = null;
        fallbackCursor = 0;
        fallbackPromise = null;
        fallbackBusy = false;
        openNative(item, currentTimeMs);
    }

    function stop() {
        generation += 1;
        currentItem = null;
        currentSession = 0;
        fallbackRows = null;
        fallbackCursor = 0;
        fallbackPromise = null;
        fallbackBusy = false;
        native.playerStop();
    }

    native.onPlayerState(function (payload) {
        if (!payload || (currentSession && payload.sessionId &&
                Number(payload.sessionId) !== currentSession)) {
            return;
        }
        if (payload.text) {
            onState(payload.text);
        }
        if (payload.error && currentItem) {
            tryNextFallback(currentItem, currentTimeMs, payload.text);
        }
    });

    native.onPlayerTime(function (payload) {
        if (!payload || (currentSession && payload.sessionId &&
                Number(payload.sessionId) !== currentSession)) {
            return;
        }
        currentTimeMs = Math.max(0, Number(payload.milliseconds) || 0);
        onTime(currentTimeMs);
    });

    function dispatchKey(payload) {
        var code = Number(payload && payload.keyCode) || 0;
        var key = String(payload && payload.key || "");
        var event = new KeyboardEvent("keydown", {
            key: key,
            code: String(payload && payload.code || ""),
            bubbles: true,
            cancelable: true
        });
        try {
            Object.defineProperty(event, "keyCode", { get: function () { return code; } });
            Object.defineProperty(event, "which", { get: function () { return code; } });
        } catch (ignoreDefine) {}
        document.dispatchEvent(event);
    }

    native.onPlayerKey(dispatchKey);

    window.BlazzingPlayer = {
        open: open,
        stop: stop,
        togglePause: function () {
            if (currentItem) { native.playerCommand("togglePause"); }
        },
        seek: function (seconds) {
            if (currentItem && currentItem.kind !== "live") {
                native.playerCommand("seek", Number(seconds) || 0);
            }
        },
        currentTime: function () { return currentTimeMs; },
        item: function () { return currentItem; },
        isAvPlay: function () { return false; },
        setStateListener: function (listener) {
            onState = listener || function () {};
        },
        setTimeListener: function (listener) {
            onTime = listener || function () {};
        }
    };
}());
