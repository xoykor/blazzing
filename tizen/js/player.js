/* SPDX-License-Identifier: MIT */
(function () {
    "use strict";

    var video = document.getElementById("html5-player");
    var avSurface = document.getElementById("av-player-object");
    var currentItem = null;
    var usingAvPlay = false;
    var activeUrl = "";
    var fallbackRows = null;
    var fallbackCursor = 0;
    var fallbackPromise = null;
    var failoverBusy = false;
    var playGeneration = 0;
    var onState = function () {};
    var onTime = function () {};

    function avPlayAvailable() {
        return !!(window.webapis && window.webapis.avplay);
    }

    function emitState(text) {
        onState(text);
    }

    function setAvSurfaceActive(active) {
        document.documentElement.classList.toggle("avplay-active", !!active);
        document.body.classList.toggle("avplay-active", !!active);
        if (avSurface) {
            avSurface.setAttribute("aria-hidden", active ? "false" : "true");
        }
    }

    function setScreenSaver(enabled) {
        var api;
        var state;

        if (!window.webapis || !window.webapis.appcommon) {
            return;
        }

        try {
            api = window.webapis.appcommon;
            state = enabled ?
                api.AppCommonScreenSaverState.SCREEN_SAVER_ON :
                api.AppCommonScreenSaverState.SCREEN_SAVER_OFF;
            api.setScreenSaver(state, function () {}, function () {});
        } catch (ignoreScreenSaver) {}
    }

    function closeAvPlay() {
        if (!avPlayAvailable()) { return; }
        try {
            var state = window.webapis.avplay.getState();
            if (state === "PLAYING" || state === "PAUSED" || state === "READY") {
                window.webapis.avplay.stop();
            }
        } catch (ignoreStop) {}
        try { window.webapis.avplay.close(); } catch (ignoreClose) {}
    }

    function stopHtml5() {
        try { video.pause(); } catch (ignorePause) {}
        video.removeAttribute("src");
        try { video.load(); } catch (ignoreLoad) {}
        video.classList.add("hidden");
    }

    function stop() {
        playGeneration += 1;
        closeAvPlay();
        stopHtml5();
        setAvSurfaceActive(false);
        setScreenSaver(true);
        currentItem = null;
        usingAvPlay = false;
        activeUrl = "";
        fallbackRows = null;
        fallbackCursor = 0;
        fallbackPromise = null;
        failoverBusy = false;
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
                if (!/^https?:\/\//i.test(url)) { return; }
                if (/workers\.dev/i.test(url)) { return; }
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

    function attemptPlayback(item, resumeMs, url) {
        activeUrl = url;
        failoverBusy = false;

        if (avPlayAvailable()) {
            try {
                openAvPlay(item, resumeMs || 0, url);
                return;
            } catch (error) {
                closeAvPlay();
                usingAvPlay = false;
                setAvSurfaceActive(false);
            }
        }
        openHtml5(item, resumeMs || 0, url);
    }

    function tryNextFallback(item, resumeMs, reason) {
        var generation = playGeneration;

        if (!item || failoverBusy) { return; }
        failoverBusy = true;

        loadFallbackRows(item).then(function (rows) {
            var row;

            if (generation !== playGeneration || currentItem !== item) {
                return;
            }

            while (fallbackCursor < rows.length) {
                row = rows[fallbackCursor++];
                if (row && row.url && row.url !== activeUrl) {
                    emitState("Fonte indisponível; tentando alternativa " +
                        fallbackCursor + "…");
                    closeAvPlay();
                    stopHtml5();
                    attemptPlayback(item, resumeMs || 0, row.url);
                    return;
                }
            }

            failoverBusy = false;
            emitState(reason || "Não foi possível reproduzir nenhuma fonte disponível.");
        });
    }

    function openAvPlay(item, resumeMs, url) {
        var listener = {
            onbufferingstart: function () { emitState("Buffering…"); },
            onbufferingprogress: function (percent) { emitState("Buffering " + percent + "%"); },
            onbufferingcomplete: function () { emitState("Reproduzindo"); },
            oncurrentplaytime: function (milliseconds) { onTime(milliseconds || 0); },
            onstreamcompleted: function () {
                setScreenSaver(true);
                emitState("Concluído");
            },
            onerror: function (eventType) {
                tryNextFallback(item, currentTime(), "Erro de reprodução: " + eventType);
            },
            onevent: function () {},
            ondrmevent: function () {},
            onsubtitlechange: function () {}
        };

        usingAvPlay = true;
        video.classList.add("hidden");
        setAvSurfaceActive(true);
        window.webapis.avplay.open(url || item.url);
        window.webapis.avplay.setListener(listener);
        window.webapis.avplay.setDisplayRect(0, 0, 1920, 1080);
        try {
            window.webapis.avplay.setDisplayMethod(
                "PLAYER_DISPLAY_MODE_LETTER_BOX"
            );
        } catch (ignoreDisplayMethod) {}
        emitState("Preparando…");

        window.webapis.avplay.prepareAsync(function () {
            if (resumeMs && resumeMs > 30000) {
                try { window.webapis.avplay.seekTo(resumeMs); } catch (ignoreSeek) {}
            }
            try {
                window.webapis.avplay.play();
                setScreenSaver(false);
                emitState("Reproduzindo");
            } catch (error) {
                emitState("Falha ao iniciar a reprodução.");
            }
        }, function (error) {
            closeAvPlay();
            usingAvPlay = false;
            setAvSurfaceActive(false);
            emitState("AVPlay falhou; tentando player alternativo…");
            openHtml5(item, resumeMs || 0, url || item.url);
        });
    }

    function openHtml5(item, resumeMs, url) {
        var promise;
        usingAvPlay = false;
        setAvSurfaceActive(false);
        video.classList.remove("hidden");
        video.src = url || item.url;
        emitState("Preparando…");
        video.addEventListener("loadedmetadata", function resumeOnce() {
            video.removeEventListener("loadedmetadata", resumeOnce);
            if (resumeMs) {
                try { video.currentTime = resumeMs / 1000; } catch (ignoreResume) {}
            }
        });
        promise = video.play();
        if (promise && promise.catch) {
            promise.catch(function () {
                tryNextFallback(item, resumeMs || 0, "Falha no player HTML5.");
            });
        }
    }

    function open(item, resumeMs) {
        stop();
        currentItem = item;
        fallbackRows = null;
        fallbackCursor = 0;
        fallbackPromise = null;
        failoverBusy = false;
        attemptPlayback(item, resumeMs || 0, item.url);
    }

    function togglePause() {
        var state;
        if (!currentItem) { return; }

        if (usingAvPlay) {
            try {
                state = window.webapis.avplay.getState();
                if (state === "PLAYING") {
                    window.webapis.avplay.pause();
                    setScreenSaver(true);
                    emitState("Pausado");
                } else if (state === "PAUSED" || state === "READY") {
                    window.webapis.avplay.play();
                    setScreenSaver(false);
                    emitState("Reproduzindo");
                }
            } catch (error) {
                emitState("Falha no play/pause.");
            }
        } else if (video.paused) {
            video.play();
            setScreenSaver(false);
        } else {
            video.pause();
            setScreenSaver(true);
        }
    }

    function seek(seconds) {
        var target;
        if (!currentItem || currentItem.kind === "live") { return; }

        if (usingAvPlay) {
            try {
                target = Math.max(0, window.webapis.avplay.getCurrentTime() + seconds * 1000);
                window.webapis.avplay.seekTo(target);
            } catch (ignoreSeek) {}
        } else {
            try { video.currentTime = Math.max(0, video.currentTime + seconds); } catch (ignoreHtmlSeek) {}
        }
    }

    function currentTime() {
        if (!currentItem) { return 0; }
        if (usingAvPlay) {
            try { return window.webapis.avplay.getCurrentTime() || 0; }
            catch (ignoreTime) { return 0; }
        }
        return Math.floor((video.currentTime || 0) * 1000);
    }

    video.addEventListener("timeupdate", function () {
        if (!usingAvPlay) { onTime(currentTime()); }
    });
    video.addEventListener("waiting", function () {
        if (!usingAvPlay) { emitState("Buffering…"); }
    });
    video.addEventListener("error", function () {
        if (!usingAvPlay && currentItem) {
            tryNextFallback(currentItem, currentTime(), "Falha no player HTML5.");
        }
    });
    video.addEventListener("playing", function () {
        if (!usingAvPlay) {
            setScreenSaver(false);
            emitState("Reproduzindo");
        }
    });
    video.addEventListener("pause", function () {
        if (!usingAvPlay) { setScreenSaver(true); }
    });
    video.addEventListener("ended", function () {
        if (!usingAvPlay) {
            setScreenSaver(true);
            emitState("Concluído");
        }
    });

    window.BlazzingPlayer = {
        open: open,
        stop: stop,
        togglePause: togglePause,
        seek: seek,
        currentTime: currentTime,
        item: function () { return currentItem; },
        isAvPlay: function () { return usingAvPlay; },
        setStateListener: function (listener) { onState = listener || function () {}; },
        setTimeListener: function (listener) { onTime = listener || function () {}; }
    };
}());
