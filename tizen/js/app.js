/* SPDX-License-Identifier: MIT */
/* Main controller for the Blazzing Samsung Tizen Web application. */
(function () {
    "use strict";

    var byId = function (id) { return document.getElementById(id); };

    function initialCatalogState() {
        return {
            kind: "live",
            catalog: { items: [], categories: [] },
            filtered: [],
            category: "all",
            favoritesOnly: false,
            visibleCount: 0,
            batchSize: 30,
            renderGeneration: 0
        };
    }

    function initialPlaybackState() {
        return {
            series: null,
            currentSeason: "all",
            playerReturnView: "catalog",
            currentPlaylistIndex: -1,
            lastProgressWrite: 0,
            returnFocusUid: "",
            catalogScrollTop: 0,
            lastZapAt: 0
        };
    }

    function initialAppState() {
        var value = initialCatalogState();
        var playback = initialPlaybackState();
        var key;

        value.mode = "m3u";
        value.view = "home";
        value.profile = null;
        value.xtream = null;
        value.m3uCatalogs = null;
        value.pairingActive = false;

        for (key in playback) {
            if (Object.prototype.hasOwnProperty.call(playback, key)) {
                value[key] = playback[key];
            }
        }
        return value;
    }

    var state = initialAppState();

    var toastTimer = 0;
    var hudTimer = 0;
    var supportedKeys = {};
    var searchTimer = 0;
    var gridAppendScheduled = false;
    var imageObserver = null;
    var imageQueue = [];
    var activeImageLoads = 0;
    var MAX_IMAGE_LOADS = 6;
    var cardShardCache = {};
    var cardShardPromises = {};
    var cardShardFailureAt = {};
    var cardShardQueue = [];
    var activeCardShardLoads = 0;
    var MAX_CARD_SHARD_LOADS = 4;
    var artworkQueue = [];
    var artworkTimer = 0;
    var artworkSequence = 0;
    var artworkInFlight = {};
    var artworkSessionCache = {};
    var MAX_ARTWORK_BATCH = 24;



    function setHidden(element, hidden) {
        element.classList.toggle("hidden", !!hidden);
    }

    function hideToast() {
        setHidden(byId("toast"), true);
    }

    function viewNames() {
        return ["home", "catalog", "series", "player"];
    }


    function showToast(message) {
        var toast = byId("toast");
        clearTimeout(toastTimer);
        toast.textContent = message;
        setHidden(toast, false);
        toastTimer = setTimeout(hideToast, 3200);
    }


    function setBusy(active, message) {
        byId("busy-text").textContent = message || "Carregando…";
        setHidden(byId("busy"), !active);
    }


    function setView(name) {
        viewNames().forEach(function (view) {
            setHidden(byId(view + "-view"), view !== name);
        });
        state.view = name;
        setTimeout(focusFirst, 0);
    }

    function focusRoot() {
        return state.pairingActive ?
            byId("pairing-modal") : byId(state.view + "-view");
    }

    function activeCatalogCardIndex() {
        var active = document.activeElement;
        var index;

        if (state.view !== "catalog" || !active ||
                !active.hasAttribute("data-card-index")) {
            return -1;
        }

        index = parseInt(active.getAttribute("data-card-index"), 10);
        return isNaN(index) ? -1 : index;
    }

    function focusableIsRelevant(element, activeIndex) {
        var cardIndex;
        var rect;

        if (element.disabled) { return false; }

        if (state.view === "catalog" &&
                element.hasAttribute("data-card-index") &&
                activeIndex >= 0) {
            cardIndex = parseInt(element.getAttribute("data-card-index"), 10);
            if (!isNaN(cardIndex) && Math.abs(cardIndex - activeIndex) > 24) {
                return false;
            }
        }

        rect = element.getBoundingClientRect();
        return rect.width > 0 && rect.height > 0 &&
            rect.bottom >= -500 && rect.top <= 1580;
    }

    function rectCenter(rect) {
        return {
            x: rect.left + rect.width / 2,
            y: rect.top + rect.height / 2
        };
    }

    function directionAllows(direction, dx, dy) {
        if (direction === "left") { return dx < -4; }
        if (direction === "right") { return dx > 4; }
        if (direction === "up") { return dy < -4; }
        return direction === "down" && dy > 4;
    }

    function navigationScore(direction, dx, dy) {
        var horizontal = direction === "left" || direction === "right";
        var primary = horizontal ? Math.abs(dx) : Math.abs(dy);
        var secondary = horizontal ? Math.abs(dy) : Math.abs(dx);
        return primary + secondary * 2.4;
    }

    function focusNavigationTarget(target) {
        if (!target) { return; }
        target.focus();
        try { target.scrollIntoView(false); } catch (ignoreScroll) {}
        maybeAppendCards(target);
    }


    function focusables() {
        var root = focusRoot();
        var activeIndex;
        var all;

        if (!root) { return []; }

        activeIndex = activeCatalogCardIndex();
        all = Array.prototype.slice.call(
            root.querySelectorAll('[data-focusable="true"]:not(.hidden)')
        );

        return all.filter(function (element) {
            return focusableIsRelevant(element, activeIndex);
        });
    }

    function focusFirst() {
        var items = focusables();
        if (items.length) {
            items[0].focus();
        }
    }


    function geometricMove(direction) {
        var current = document.activeElement;
        var items = focusables();
        var origin;
        var best = null;
        var bestScore = Infinity;

        if (!current || items.indexOf(current) === -1) {
            focusFirst();
            return;
        }

        origin = rectCenter(current.getBoundingClientRect());
        items.forEach(function (candidate) {
            var point;
            var dx;
            var dy;
            var score;

            if (candidate === current) { return; }

            point = rectCenter(candidate.getBoundingClientRect());
            dx = point.x - origin.x;
            dy = point.y - origin.y;
            if (!directionAllows(direction, dx, dy)) { return; }

            score = navigationScore(direction, dx, dy);
            if (score < bestScore) {
                best = candidate;
                bestScore = score;
            }
        });

        focusNavigationTarget(best);
    }


    function remoteKeyNames() {
        return [
            "MediaPlay",
            "MediaPause",
            "MediaStop",
            "MediaPlayPause",
            "MediaFastForward",
            "MediaRewind",
            "ColorF2Yellow"
        ];
    }

    function rememberSupportedRemoteKeys() {
        window.tizen.tvinputdevice.getSupportedKeys().forEach(function (key) {
            supportedKeys[key.code] = key.name;
        });
    }

    function registerRemoteKeyNames(names) {
        if (window.tizen.tvinputdevice.registerKeyBatch) {
            window.tizen.tvinputdevice.registerKeyBatch(names);
            return;
        }
        names.forEach(function (name) {
            try { window.tizen.tvinputdevice.registerKey(name); }
            catch (ignoreKey) {}
        });
    }


    function registerRemoteKeys() {
        if (!window.tizen || !window.tizen.tvinputdevice) { return; }

        try { rememberSupportedRemoteKeys(); }
        catch (ignoreSupported) {}

        try { registerRemoteKeyNames(remoteKeyNames()); }
        catch (ignoreBatch) {}
    }


    function closeNativeWindow() {
        if (!window.BlazzingWindowsNative ||
                !window.BlazzingWindowsNative.close) {
            return false;
        }
        window.BlazzingWindowsNative.close();
        return true;
    }

    function closeTizenApplication() {
        if (!window.tizen || !window.tizen.application) { return false; }
        try {
            window.tizen.application.getCurrentApplication().exit();
            return true;
        } catch (ignoreExit) {
            return false;
        }
    }


    function exitApplication() {
        if (closeNativeWindow() || closeTizenApplication()) { return; }
        showToast("Use o botão Home para sair.");
    }

    function goBack() {
        if (state.pairingActive) {
            cancelPairing();
        } else if (state.view === "player") {
            closePlayer();
        } else if (state.view === "series") {
            setView("catalog");
        } else if (state.view === "catalog") {
            /*
             * O botão Back do controle não abandona a playlist ativa.
             * A troca/saída para a tela de perfis é uma ação explícita pelo
             * botão "Listas" no topo do catálogo.
             */
            showToast("Use “Listas” para trocar de playlist.");
        } else {
            exitApplication();
        }
    }

    function isBackKey(name, keyCode) {
        return name === "Back" || keyCode === 10009 || keyCode === 27;
    }

    function playerKeyAction(name, keyCode) {
        if (isBackKey(name, keyCode)) { return "back"; }
        if (name === "MediaPlayPause" || name === "MediaPlay" ||
                name === "MediaPause" || keyCode === 13 || keyCode === 32 ||
                keyCode === 415 || keyCode === 19) {
            return "toggle";
        }
        if (name === "MediaStop" || keyCode === 413) { return "stop"; }
        if (keyCode === 37 || name === "MediaRewind" || keyCode === 412) {
            return "rewind";
        }
        if (keyCode === 39 || name === "MediaFastForward" || keyCode === 417) {
            return "forward";
        }
        if (keyCode === 38) { return "volume-up"; }
        if (keyCode === 40) { return "volume-down"; }
        return "";
    }

    function seekOrZap(seconds, liveDelta) {
        var item = window.BlazzingPlayer.item();
        if (item && item.kind === "live") {
            switchLive(liveDelta);
        } else {
            window.BlazzingPlayer.seek(seconds);
        }
        showHud();
    }

    function runPlayerAction(action) {
        if (action === "back") {
            goBack();
        } else if (action === "toggle") {
            window.BlazzingPlayer.togglePause();
            showHud();
        } else if (action === "stop") {
            closePlayer();
        } else if (action === "rewind") {
            seekOrZap(-10, -1);
        } else if (action === "forward") {
            seekOrZap(10, 1);
        } else if ((action === "volume-up" || action === "volume-down") &&
                window.BlazzingPlayer.adjustVolume) {
            window.BlazzingPlayer.adjustVolume(action === "volume-up" ? 5 : -5);
            showHud();
        } else {
            return false;
        }
        return true;
    }

    function handlePlayerKey(name, keyCode) {
        var handled = runPlayerAction(playerKeyAction(name, keyCode));
        if (!handled) { showHud(); }
        return handled;
    }

    function focusedCatalogItem() {
        var active = document.activeElement;
        var index;
        if (state.view !== "catalog" || !active) {
            return null;
        }
        index = parseInt(active.getAttribute("data-card-index"), 10);
        if (isNaN(index) || index < 0 || index >= state.filtered.length) {
            return null;
        }
        return state.filtered[index];
    }

    function refreshFavoriteBadge(uid, on) {
        var selector = '.media-card[data-item-uid="' +
            String(uid || "").replace(/"/g, '\\"') + '"] .favorite';
        var button;
        try {
            button = document.querySelector(selector);
        } catch (ignoreSelector) {
            button = null;
        }
        if (button) {
            button.classList.toggle("on", !!on);
        }
    }

    function focusCatalogIndex(index) {
        var grid = byId("catalog-grid");
        var target;
        var safeIndex;

        if (!grid || !state.filtered.length) {
            return;
        }

        safeIndex = Math.max(
            0,
            Math.min(Number(index) || 0, state.filtered.length - 1)
        );

        while (state.visibleCount <= safeIndex &&
                state.visibleCount < state.filtered.length) {
            appendGridBatch();
        }

        target = grid.querySelector(
            '.card-main[data-card-index="' + safeIndex + '"]'
        );

        if (target) {
            target.focus();
            try { target.scrollIntoView(false); } catch (ignoreScroll) {}
            maybeAppendCards(target);
        }
    }

    function toggleFocusedFavorite() {
        var active = document.activeElement;
        var item = focusedCatalogItem();
        var index = active ?
            parseInt(active.getAttribute("data-card-index"), 10) : 0;
        var on;
        if (!item) {
            showToast("Selecione um card para favoritar.");
            return;
        }

        on = window.BlazzingStorage.toggleFavorite(item.uid);
        refreshFavoriteBadge(item.uid, on);
        showToast(on ? "Adicionado aos favoritos." : "Removido dos favoritos.");

        if (state.favoritesOnly && !on) {
            applyFilters();
            setTimeout(function () {
                focusCatalogIndex(isNaN(index) ? 0 : index);
            }, 0);
        }
    }

    function handleWindowsShortcut(event, code) {
        var section;
        var sectionButton;
        if (!window.BlazzingWindowsNative || !event.ctrlKey || event.altKey ||
                state.view !== "catalog") {
            return false;
        }
        if (code === 68) {
            toggleFocusedFavorite();
        } else if (code === 70) {
            byId("catalog-search").focus();
        } else if (code === 76) {
            byId("lists-button").click();
        } else if (code === 49 || code === 50 || code === 51) {
            section = code === 49 ? "live" : (code === 50 ? "vod" : "series");
            sectionButton = document.querySelector('[data-section="' + section + '"]');
            if (sectionButton) { sectionButton.click(); }
        } else {
            return false;
        }
        event.preventDefault();
        return true;
    }

    function handleCatalogCommand(event, code, name) {
        if (state.view !== "catalog" ||
                (name !== "ColorF2Yellow" && code !== 405)) {
            return false;
        }
        event.preventDefault();
        toggleFocusedFavorite();
        return true;
    }

    function handleActivationKey(event, code, active, isInput) {
        if (code !== 13 || isInput) { return false; }
        if (active && active.click) {
            event.preventDefault();
            active.click();
        }
        return true;
    }

    function directionForKey(code) {
        if (code === 37) { return "left"; }
        if (code === 38) { return "up"; }
        if (code === 39) { return "right"; }
        if (code === 40) { return "down"; }
        return "";
    }

    function handleDirectionalKey(event, code, isInput) {
        var direction;
        if (isInput && (code === 37 || code === 39)) { return true; }
        direction = directionForKey(code);
        if (!direction) { return false; }
        event.preventDefault();
        geometricMove(direction);
        return true;
    }

    function handleNavigationKey(event, code, name, active, isInput) {
        if (isBackKey(name, code)) {
            event.preventDefault();
            goBack();
            return true;
        }
        return handleCatalogCommand(event, code, name) ||
            handleActivationKey(event, code, active, isInput) ||
            handleDirectionalKey(event, code, isInput);
    }

    document.addEventListener("keydown", function (event) {
        var code = event.keyCode || event.which;
        var name = supportedKeys[code] || event.key || "";
        var active = document.activeElement;
        var isInput = active &&
            (active.tagName === "INPUT" || active.tagName === "TEXTAREA");

        if (state.view === "player") {
            if (handlePlayerKey(name, code)) { event.preventDefault(); }
            return;
        }
        if (handleWindowsShortcut(event, code)) { return; }
        handleNavigationKey(event, code, name, active, isInput);
    });


    function bindModeButton(id, mode) {
        byId(id).addEventListener("click", function () { setMode(mode); });
    }

    function setMode(mode) {
        state.mode = mode;
        byId("mode-m3u").classList.toggle("active", mode === "m3u");
        byId("mode-xtream").classList.toggle("active", mode === "xtream");
        byId("m3u-fields").classList.toggle("hidden", mode !== "m3u");
        byId("xtream-fields").classList.toggle("hidden", mode !== "xtream");
        byId("save-secret-row").classList.toggle("hidden", mode !== "xtream");
    }

    bindModeButton("mode-m3u", "m3u");
    bindModeButton("mode-xtream", "xtream");

    function clearPairingDisplay() {
        byId("pairing-qr").innerHTML = "";
        byId("pairing-url").textContent = "";
    }

    function setPairingVisible(visible) {
        state.pairingActive = !!visible;
        byId("pairing-modal").classList.toggle("hidden", !visible);
    }

    function setPairingStatus(message, kind) {
        var target = byId("pairing-status");
        var retry = byId("pairing-retry");
        if (!target) { return; }

        target.textContent = message || "";
        target.setAttribute("data-state", kind || "");
        if (retry) { retry.classList.toggle("hidden", kind !== "expired"); }

        if (kind === "expired") {
            clearPairingDisplay();
            setTimeout(function () {
                if (state.pairingActive && retry) { retry.focus(); }
            }, 0);
        }
    }

    function cancelPairing() {
        if (window.BlazzingPairing) { window.BlazzingPairing.stop(); }
        setPairingVisible(false);
        setTimeout(focusFirst, 0);
    }

    function pairedProfile(profile) {
        return {
            type: "m3u",
            name: profile.name || "Lista do celular",
            url: profile.url
        };
    }

    function acceptPairedPlaylist(profile) {
        var normalized = pairedProfile(profile);
        setPairingVisible(false);
        setMode("m3u");
        byId("profile-name").value = normalized.name;
        byId("m3u-url").value = normalized.url;
        byId("save-profile").checked = true;
        connectM3u(normalized, false);
    }

    function showPairingSession(session) {
        if (!state.pairingActive) { return; }
        byId("pairing-qr").innerHTML = session.qrSvg;
        byId("pairing-url").textContent = session.url;
    }

    function showPairingError(error) {
        if (!state.pairingActive) { return; }
        setPairingStatus(
            error.message || "Não foi possível iniciar o pareamento.",
            "error"
        );
    }

    function startPairing() {
        if (!window.BlazzingPairing) {
            showToast("O módulo de pareamento não foi carregado.");
            return;
        }

        setPairingVisible(true);
        clearPairingDisplay();
        setPairingStatus("Preparando sessão segura…", "creating");
        setTimeout(focusFirst, 0);

        window.BlazzingPairing.start(
            acceptPairedPlaylist,
            setPairingStatus
        ).then(showPairingSession).catch(showPairingError);
    }

    byId("pair-button").addEventListener("click", startPairing);
    byId("pairing-retry").addEventListener("click", startPairing);
    byId("pairing-cancel").addEventListener("click", cancelPairing);


    function trimmedField(id) {
        return byId(id).value.replace(/^\s+|\s+$/g, "");
    }

    function m3uProfileFromForm(name) {
        return {
            type: "m3u",
            name: name,
            url: trimmedField("m3u-url")
        };
    }

    function xtreamProfileFromForm(name) {
        return {
            type: "xtream",
            name: name,
            server: trimmedField("xtream-server"),
            alternate: trimmedField("xtream-alternate"),
            username: byId("xtream-user").value,
            password: byId("xtream-password").value
        };
    }


    function profileFromForm() {
        var name = trimmedField("profile-name") || "Minha lista";
        return state.mode === "m3u" ?
            m3uProfileFromForm(name) : xtreamProfileFromForm(name);
    }

    function saveProfileIfRequested(profile) {
        var copy;
        var saved;

        if (profile.type !== "m3u" && !byId("save-profile").checked) {
            return profile;
        }

        copy = JSON.parse(JSON.stringify(profile));
        if (copy.type === "xtream" && !byId("save-secret").checked) {
            copy.password = "";
        }

        saved = window.BlazzingStorage.saveProfile(copy);
        renderSavedProfiles();
        return saved || profile;
    }

    function normalizedM3uUrl(value) {
        return String(value || "").replace(/^\s+|\s+$/g, "");
    }

    function setPlaylistRefreshVisible(visible) {
        var button = byId("refresh-playlist-button");
        if (button) {
            button.classList.toggle("hidden", !visible);
        }
    }

    var AUTO_OPEN_IN_PROGRESS_KEY = "blazzing.tizen.auto-open-in-progress";
    var AUTO_OPEN_SUPPRESSED_KEY = "blazzing.tizen.auto-open-suppressed";

    function rememberOpenedM3u(profile) {
        if (profile && profile.id) {
            window.BlazzingStorage.setLastOpenedProfileId(profile.id);
        }
    }

    function realTizenDevice() {
        return !!(window.tizen && !window.BlazzingWindowsNative);
    }

    function localFlag(key) {
        try { return window.localStorage.getItem(key) || ""; }
        catch (ignoreReadFlag) { return ""; }
    }

    function setLocalFlag(key, value) {
        try {
            if (value) { window.localStorage.setItem(key, value); }
            else { window.localStorage.removeItem(key); }
        } catch (ignoreWriteFlag) {}
    }

    function clearAutoOpenGuard() {
        setLocalFlag(AUTO_OPEN_IN_PROGRESS_KEY, "");
        setLocalFlag(AUTO_OPEN_SUPPRESSED_KEY, "");
    }

    function sameM3uSession(profile) {
        return !!(
            state.m3uCatalogs &&
            state.profile &&
            state.profile.type === "m3u" &&
            profile &&
            profile.type === "m3u" &&
            normalizedM3uUrl(state.profile.url) === normalizedM3uUrl(profile.url)
        );
    }

    function m3uParserOptions(kind) {
        if (kind === "series") {
            return {
                onlyKind: "series",
                seriesSummaryOnly: true
            };
        }
        return { onlyKind: kind };
    }

    function parseStoredM3uSection(profile, kind, options) {
        var parser = window.BlazzingProviders.createM3uParser(
            profile.url,
            options
        );
        var summaryOnly = kind === "series" && !!options.seriesSummaryOnly;

        return window.BlazzingStorage.streamCachedPlaylistSection(
            profile.url,
            kind,
            summaryOnly,
            function (chunk) {
                parser.consumeTextChunk(chunk);
            }
        ).then(function (row) {
            var catalogs;

            if (!row) { return null; }
            if (!parser.hasM3uMarker()) {
                throw new Error("cache M3U inválido");
            }

            catalogs = parser.finish();
            return catalogs[kind] || { items: [], categories: [] };
        });
    }

    function reportPlaylistIndexProgress(readChars) {
        var mib = Math.floor(readChars / (1024 * 1024));
        setBusy(true, "Otimizando playlist… " + mib + " MiB");
    }

    function buildStoredM3uSectionCaches(profile) {
        /*
         * A primeira abertura após baixar/atualizar a M3U faz uma única
         * varredura grande e cria arquivos menores para TV, Filmes e Séries.
         * As trocas seguintes leem apenas a seção necessária.
         */
        setBusy(true, "Otimizando playlist para a TV…");
        return window.BlazzingStorage.buildPlaylistSectionCaches(
            profile.url,
            window.BlazzingProviders.classifyM3uEntry,
            reportPlaylistIndexProgress
        );
    }

    function rebuildStoredM3uCatalog(profile, kind, options) {
        return buildStoredM3uSectionCaches(profile).then(function () {
            return parseStoredM3uSection(profile, kind, options);
        }).then(function (catalog) {
            if (!catalog) {
                throw new Error("Falha ao criar índice da playlist.");
            }
            return catalog;
        });
    }

    function loadStoredM3uCatalog(profile, kind, options) {
        options = options || m3uParserOptions(kind);

        return parseStoredM3uSection(profile, kind, options).then(function (catalog) {
            if (catalog) { return catalog; }

            /*
             * Se nem o índice nem a playlist bruta existem, esta é uma
             * primeira aquisição (ou uma atualização do app que preservou o
             * perfil, mas não o arquivo privado). Retorne null para que
             * connectM3u() siga o fluxo normal de download.
             */
            return window.BlazzingStorage.cachedPlaylistExists(
                profile.url
            ).then(function (exists) {
                if (!exists) { return null; }
                return rebuildStoredM3uCatalog(profile, kind, options);
            });
        });
    }

    function activateM3u(profile, catalog, kind) {
        var saved = saveProfileIfRequested(profile) || profile;

        state.profile = saved;
        state.m3uCatalogs = {};
        state.m3uCatalogs[kind] = catalog;
        state.xtream = null;
        rememberOpenedM3u(saved);
        clearAutoOpenGuard();
        setPlaylistRefreshVisible(true);
        setBusy(false);
        openCatalog(kind || "live");
    }

    function resumeM3uSession(profile) {
        var saved = saveProfileIfRequested(profile) || profile;
        state.profile = saved;
        state.xtream = null;
        rememberOpenedM3u(saved);
        setPlaylistRefreshVisible(true);
        openCatalog(state.kind || "live");
    }

    function openStoredM3u(profile, kind) {
        return loadStoredM3uCatalog(profile, kind).then(function (catalog) {
            if (!catalog) {
                return false;
            }
            activateM3u(profile, catalog, kind);
            return true;
        });
    }

    function showM3uDownloadError(error) {
        setBusy(false);
        showToast(error.message || "Falha ao baixar ou armazenar a playlist.");
    }

    function playlistDownloadProgress(refreshing, receivedSize, totalSize) {
        var receivedMiB = Math.floor(receivedSize / (1024 * 1024));
        var totalMiB = totalSize > 0 ?
            Math.floor(totalSize / (1024 * 1024)) : 0;
        var prefix = refreshing ? "Atualizando playlist… " : "Baixando playlist… ";

        setBusy(
            true,
            prefix + receivedMiB + (totalMiB ? "/" + totalMiB : "") + " MiB"
        );
    }

    function downloadM3uToTizenCache(profile, kind, refreshing) {
        /*
         * O XMLHttpRequest mantém responseText inteiro na heap JavaScript.
         * Em uma playlist de ~85 MiB isso pode derrubar o Web Runtime da TV.
         * O Download API do Tizen grava direto no filesystem e mantém a
         * playlist fora da heap durante a transferência.
         */
        return window.BlazzingStorage.downloadPlaylistToCache(profile.url, {
            timeout: 180000,
            maxBytes: window.BlazzingNet.MAX_RESPONSE_BYTES,
            onProgress: function (receivedSize, totalSize) {
                playlistDownloadProgress(refreshing, receivedSize, totalSize);
            }
        }).then(function () {
            return openStoredM3u(profile, kind);
        });
    }

    function cacheDownloadedM3u(profile, text) {
        if (text.indexOf("#EXTM3U") === -1 &&
                text.indexOf("#EXTINF:") === -1) {
            throw new Error("O conteúdo recebido não parece ser uma playlist M3U.");
        }
        return window.BlazzingStorage.saveCachedPlaylist(profile.url, text);
    }

    function downloadM3uInBrowser(profile, kind) {
        /*
         * Fallback para navegador/ambiente de desenvolvimento. Em TV real
         * nunca caímos neste XHR, pois materializar listas gigantes em
         * responseText é justamente o comportamento que queremos evitar.
         */
        return window.BlazzingNet.text(profile.url, {
            timeout: 60000,
            maxBytes: window.BlazzingNet.MAX_RESPONSE_BYTES
        }).then(function (text) {
            return cacheDownloadedM3u(profile, text);
        }).then(function () {
            return openStoredM3u(profile, kind);
        });
    }

    function downloadAndStoreM3u(profile, kind, refreshing) {
        var download;

        setBusy(true, refreshing ? "Atualizando playlist…" : "Baixando playlist pela primeira vez…");
        download = realTizenDevice() ?
            downloadM3uToTizenCache(profile, kind, refreshing) :
            downloadM3uInBrowser(profile, kind);
        download.catch(showM3uDownloadError);
    }

    function connectM3u(profile, forceReload, kind) {
        profile.url = normalizedM3uUrl(profile.url);

        if (!/^https?:\/\//i.test(profile.url)) {
            showToast("Informe uma URL M3U/M3U8 HTTP ou HTTPS.");
            return;
        }

        if (!forceReload && sameM3uSession(profile)) {
            resumeM3uSession(profile);
            return;
        }

        if (forceReload) {
            downloadAndStoreM3u(profile, kind || state.kind || "live", true);
            return;
        }

        setBusy(true, "Abrindo playlist salva…");
        openStoredM3u(profile, kind || "live").then(function (opened) {
            if (opened) {
                return;
            }

            /*
             * Sem cópia persistida ainda: esta é a aquisição inicial.
             * Depois disso, abrir/conectar usa somente o armazenamento local;
             * uma nova requisição só acontece pelo botão Atualizar playlist.
             */
            downloadAndStoreM3u(profile, kind || "live", false);
        }).catch(function (error) {
            setBusy(false);
            if (String(error && error.message || "").indexOf("cache M3U inválido") !== -1) {
                showToast("Playlist salva inválida. Use Atualizar playlist.");
                return;
            }
            showToast(error.message || "Não foi possível ler a playlist salva.");
        });
    }

    function connectXtream(profile) {
        var client;

        if (!profile.server || !profile.username || !profile.password) {
            showToast("Servidor, usuário e senha Xtream são obrigatórios.");
            return;
        }

        client = new window.BlazzingProviders.XtreamClient(profile);
        setBusy(true, "Autenticando no Xtream…");

        client.authenticate().then(function () {
            state.profile = saveProfileIfRequested(profile) || profile;
            state.xtream = client;
            state.m3uCatalogs = null;
            setPlaylistRefreshVisible(false);
            return loadCatalog("live");
        }).then(function () {
            setBusy(false);
            setView("catalog");
            renderCatalog();
        }).catch(function (error) {
            setBusy(false);
            showToast(error.message || "Falha ao conectar ao Xtream.");
        });
    }

    byId("connect-form").addEventListener("submit", function (event) {
        event.preventDefault();
        connectProfile(profileFromForm());
    });


    function connectProfile(profile) {
        if (profile.type === "m3u") {
            connectM3u(profile, false, "live");
        } else {
            connectXtream(profile);
        }
    }

    function loadProfile(profile) {
        setMode(profile.type);
        byId("profile-name").value = profile.name || "";

        if (profile.type === "m3u") {
            byId("m3u-url").value = profile.url || "";
            connectM3u(profile, false, "live");
            return;
        }

        byId("xtream-server").value = profile.server || "";
        byId("xtream-alternate").value = profile.alternate || "";
        byId("xtream-user").value = profile.username || "";
        byId("xtream-password").value = profile.password || "";

        if (!profile.password) {
            showToast("Este perfil não tem senha salva. Informe a senha e conecte.");
            byId("xtream-password").focus();
            return;
        }

        connectXtream(profile);
    }


    function savedProfileMeta(profile) {
        return profile.type === "m3u" ?
            "M3U / M3U8 · armazenada na TV" : "Xtream Codes";
    }

    function makeSavedProfileInfo(profile) {
        var info = document.createElement("div");
        var name = document.createElement("strong");
        var meta = document.createElement("small");

        name.textContent = profile.name || "Perfil";
        meta.textContent = savedProfileMeta(profile);
        info.appendChild(name);
        info.appendChild(meta);
        return info;
    }

    function makeSavedProfileAction(label, action) {
        var button = document.createElement("button");
        button.textContent = label;
        button.setAttribute("data-focusable", "true");
        button.addEventListener("click", action);
        return button;
    }

    function makeSavedProfileCard(profile) {
        var card = document.createElement("div");
        var actions = document.createElement("div");
        var open;
        var remove;

        card.className = "saved-card";
        actions.className = "saved-actions";

        open = makeSavedProfileAction(
            sameM3uSession(profile) ? "Continuar" : "Abrir",
            function () { loadProfile(profile); }
        );
        remove = makeSavedProfileAction("Excluir", function () {
            window.BlazzingStorage.removeProfile(profile.id);
            renderSavedProfiles();
            focusFirst();
        });

        actions.appendChild(open);
        actions.appendChild(remove);
        card.appendChild(makeSavedProfileInfo(profile));
        card.appendChild(actions);
        return card;
    }


    function renderSavedProfiles() {
        var profiles = window.BlazzingStorage.profiles();
        var root = byId("saved-profiles");

        root.innerHTML = "";
        byId("saved-count").textContent = String(profiles.length);

        if (!profiles.length) {
            var empty = document.createElement("p");
            empty.className = "privacy-note";
            empty.textContent = "Nenhum perfil salvo.";
            root.appendChild(empty);
            return;
        }

        profiles.forEach(function (profile) {
            root.appendChild(makeSavedProfileCard(profile));
        });
    }

    function sectionTitle(kind) {
        if (kind === "vod") { return "Filmes"; }
        if (kind === "series") { return "Séries"; }
        return "TV ao vivo";
    }


    function catalogLoadMessage(kind) {
        return "Carregando " + sectionTitle(kind).toLowerCase() + "…";
    }

    function finishCatalogLoad(catalog) {
        state.catalog = catalog;
        setBusy(false);
        return catalog;
    }

    function failCatalogLoad(error) {
        setBusy(false);
        throw error;
    }

    function loadM3uCatalogSection(kind) {
        state.catalog = { items: [], categories: [] };
        state.filtered = [];
        state.m3uCatalogs = {};
        setBusy(true, catalogLoadMessage(kind));

        return loadStoredM3uCatalog(state.profile, kind).then(function (catalog) {
            if (!catalog) {
                throw new Error("A playlist salva não foi encontrada.");
            }
            state.m3uCatalogs[kind] = catalog;
            return finishCatalogLoad(catalog);
        }).catch(failCatalogLoad);
    }

    function loadXtreamCatalogSection(kind) {
        if (!state.xtream) {
            return Promise.reject(new Error("Nenhum provider carregado."));
        }

        setBusy(true, catalogLoadMessage(kind));
        return state.xtream.load(kind).then(finishCatalogLoad).catch(failCatalogLoad);
    }


    function loadCatalog(kind) {
        state.kind = kind;
        state.category = "all";
        state.favoritesOnly = false;
        byId("favorites-button").classList.remove("active");

        if (!state.m3uCatalogs) {
            return loadXtreamCatalogSection(kind);
        }
        if (state.m3uCatalogs[kind]) {
            state.catalog = state.m3uCatalogs[kind];
            return Promise.resolve(state.catalog);
        }
        return loadM3uCatalogSection(kind);
    }

    function openCatalog(kind) {
        loadCatalog(kind).then(function () {
            setView("catalog");
            renderCatalog();
        }).catch(function (error) {
            showToast(error.message || "Falha ao carregar catálogo.");
        });
    }

    bindCatalogSections();
    byId("favorites-button").addEventListener("click", toggleFavoritesFilter);
    byId("refresh-playlist-button").addEventListener("click", refreshCurrentPlaylist);
    byId("lists-button").addEventListener("click", showProfileList);
    byId("catalog-search").addEventListener("input", scheduleCatalogSearch);

    function focusCategoryButton(id) {
        var buttons = byId("categories").querySelectorAll("[data-category-id]");
        var i;
        for (i = 0; i < buttons.length; i += 1) {
            if (buttons[i].getAttribute("data-category-id") === id) {
                buttons[i].focus();
                return;
            }
        }
    }

    function updateCategorySelection() {
        var buttons = byId("categories").querySelectorAll("[data-category-id]");
        var i;
        for (i = 0; i < buttons.length; i += 1) {
            buttons[i].classList.toggle(
                "active",
                buttons[i].getAttribute("data-category-id") === state.category
            );
        }
    }


    function catalogCategoryCounts() {
        var counts = {};
        state.catalog.items.forEach(function (item) {
            counts[item.categoryId] = (counts[item.categoryId] || 0) + 1;
        });
        return counts;
    }

    function chooseCategory(id) {
        state.category = id;
        updateCategorySelection();
        applyFilters();
        setTimeout(function () { focusCategoryButton(id); }, 0);
    }

    function makeCategoryButton(id, label) {
        var button = document.createElement("button");
        button.textContent = label;
        button.setAttribute("data-focusable", "true");
        button.setAttribute("data-category-id", id);
        button.className = state.category === id ? "active" : "";
        button.addEventListener("click", function () { chooseCategory(id); });
        return button;
    }


    function renderCategories() {
        var root = byId("categories");
        var counts = catalogCategoryCounts();

        root.innerHTML = "";
        root.appendChild(makeCategoryButton(
            "all",
            "Todos (" + state.catalog.items.length + ")"
        ));

        state.catalog.categories.forEach(function (category) {
            var count = counts[category.id] || 0;
            root.appendChild(makeCategoryButton(
                category.id,
                (category.name || "Outros") + " (" + count + ")"
            ));
        });
    }

    function searchableText(item, normalize) {
        if (!item._searchText) {
            item._searchText = normalize(
                String(item.name || "") + " " +
                String(item.categoryName || item.group || "")
            );
        }
        return item._searchText;
    }

    function applyFilters() {
        var normalize = window.BlazzingProviders.normalizeWords || function (value) {
            return String(value || "").toLowerCase();
        };
        var query = normalize(byId("catalog-search").value);
        var favorites = window.BlazzingStorage.favorites();

        state.filtered = state.catalog.items.filter(function (item) {
            var categoryOk = state.category === "all" ||
                item.categoryId === state.category;
            var queryOk = !query ||
                searchableText(item, normalize).indexOf(query) !== -1;
            var favoriteOk = !state.favoritesOnly || favorites[item.uid];

            return categoryOk && queryOk && favoriteOk;
        });

        renderGrid(true);
        byId("item-count").textContent = state.filtered.length + " itens";
    }

    function renderCatalog() {
        Array.prototype.forEach.call(
            document.querySelectorAll("[data-section]"),
            function (button) {
                button.classList.toggle(
                    "active",
                    button.getAttribute("data-section") === state.kind
                );
            }
        );

        byId("content-title").textContent = sectionTitle(state.kind);
        byId("content-eyebrow").textContent = state.profile ?
            state.profile.name : "Catálogo";
        byId("catalog-search").value = "";

        renderCategories();
        updateCategorySelection();
        applyFilters();
    }

    function safeImageUrl(url) {
        url = String(url || "").replace(/^\s+|\s+$/g, "");
        if (/^https?:\/\//i.test(url)) { return url; }
        if (/^\/\//.test(url)) { return "https:" + url; }
        return "";
    }

    function posterFallback(name) {
        var span = document.createElement("span");
        span.className = "poster-fallback";
        span.textContent = String(name || "?").charAt(0).toUpperCase();
        return span;
    }

    function pumpImageQueue() {
        while (activeImageLoads < MAX_IMAGE_LOADS && imageQueue.length) {
            (function (task) {
                var image = task.image;
                var finished = false;

                if (!image || !image.parentNode ||
                        task.generation !== state.renderGeneration) {
                    return;
                }

                activeImageLoads += 1;

                function finishImageLoad(success) {
                    if (finished) { return; }
                    finished = true;
                    if (task.timeout) {
                        clearTimeout(task.timeout);
                        task.timeout = 0;
                    }
                    activeImageLoads = Math.max(0, activeImageLoads - 1);

                    if (image && image.parentNode &&
                            task.generation === state.renderGeneration) {
                        image.classList.toggle("loaded", !!success);
                        image.classList.toggle("failed", !success);
                        if (!success) {
                            try { image.parentNode.removeChild(image); }
                            catch (ignoreRemove) {}
                        }
                    }

                    setTimeout(pumpImageQueue, 0);
                }

                image.onload = function () { finishImageLoad(true); };
                image.onerror = function () { finishImageLoad(false); };

                /* Avoid keeping broken remote image requests alive forever. */
                task.timeout = setTimeout(function () {
                    if (!finished) {
                        try { image.src = ""; } catch (ignoreAbort) {}
                        finishImageLoad(false);
                    }
                }, 12000);

                image.src = task.url;
            }(imageQueue.shift()));
        }
    }

    function enqueueImage(image, url) {
        if (!image || !url || image.getAttribute("data-image-queued") === "1") {
            return;
        }
        image.setAttribute("data-image-queued", "1");
        imageQueue.push({
            image: image,
            url: url,
            generation: state.renderGeneration,
            timeout: 0
        });
        pumpImageQueue();
    }

    function observeImage(image, url) {
        image.setAttribute("data-src", url);

        if (imageObserver) {
            imageObserver.observe(image);
        } else {
            enqueueImage(image, url);
        }
    }

    function resetImagePipeline() {
        /*
         * Do not zero activeImageLoads here. Requests from the previous render
         * can still be in flight; resetting the counter would let a second
         * batch start on top of them and overload older Tizen networking.
         */
        imageQueue = [];

        if (imageObserver) {
            try { imageObserver.disconnect(); } catch (ignoreDisconnect) {}
        }

        if (window.IntersectionObserver) {
            imageObserver = new IntersectionObserver(function (entries) {
                entries.forEach(function (entry) {
                    var image;
                    var url;
                    if (!entry.isIntersecting) { return; }
                    image = entry.target;
                    url = image.getAttribute("data-src") || "";
                    try { imageObserver.unobserve(image); } catch (ignoreUnobserve) {}
                    enqueueImage(image, url);
                });
            }, {
                root: byId("catalog-grid").parentNode,
                rootMargin: "650px 0px",
                threshold: 0.01
            });
        } else {
            imageObserver = null;
        }
    }

    function pumpCardShardQueue() {
        while (activeCardShardLoads < MAX_CARD_SHARD_LOADS &&
                cardShardQueue.length) {
            (function (task) {
                activeCardShardLoads += 1;

                window.BlazzingNet.json(task.url, {
                    timeout: 20000,
                    maxBytes: 4 * 1024 * 1024
                }).then(function (rows) {
                    task.resolve(rows);
                }).catch(function (error) {
                    task.reject(error);
                }).then(function () {
                    activeCardShardLoads = Math.max(
                        0,
                        activeCardShardLoads - 1
                    );
                    setTimeout(pumpCardShardQueue, 0);
                }, function () {
                    activeCardShardLoads = Math.max(
                        0,
                        activeCardShardLoads - 1
                    );
                    setTimeout(pumpCardShardQueue, 0);
                });
            }(cardShardQueue.shift()));
        }
    }

    function queueCardShard(url) {
        return new Promise(function (resolve, reject) {
            cardShardQueue.push({
                url: url,
                resolve: resolve,
                reject: reject
            });
            pumpCardShardQueue();
        });
    }

    function artworkServiceBase() {
        if (window.BlazzingPairing && window.BlazzingPairing.baseUrl) {
            return String(window.BlazzingPairing.baseUrl() || "").replace(/\/+$/, "");
        }
        return "";
    }

    function artworkKind(item) {
        if (item && item.kind === "series") { return "tv"; }
        if (item && item.kind === "vod") { return "movie"; }
        return "auto";
    }

    function artworkLookupKey(item) {
        return artworkKind(item) + "|" + String(item && item.name || "")
            .toLowerCase().replace(/^\s+|\s+$/g, "");
    }

    function scheduleArtworkFlush() {
        if (artworkTimer) { return; }
        artworkTimer = setTimeout(function () {
            artworkTimer = 0;
            flushArtworkQueue();
        }, 40);
    }

    function finishArtworkTask(task, url, remember) {
        url = safeImageUrl(url);
        if (remember) {
            artworkSessionCache[task.cacheKey] = url || "";
        }
        if (url) {
            task.item.logo = url;
        }
        delete artworkInFlight[task.cacheKey];
        task.resolve(url || "");
    }

    function artworkPayload(batch) {
        return {
            items: batch.map(function (task) {
                return {
                    id: task.requestId,
                    title: task.item.name || "",
                    kind: artworkKind(task.item)
                };
            })
        };
    }

    function finishArtworkBatch(batch, result) {
        var byRequest = {};
        var rows = result && Array.isArray(result.items) ? result.items : [];
        rows.forEach(function (row) {
            byRequest[String(row.id || "")] = row.url || "";
        });
        batch.forEach(function (task) {
            finishArtworkTask(task, byRequest[task.requestId] || "", true);
        });
    }

    function failArtworkBatch(batch) {
        batch.forEach(function (task) {
            /* Network failures are intentionally not remembered so a later
               viewport visit can retry the central resolver. */
            finishArtworkTask(task, "", false);
        });
    }

    function scheduleNextArtworkBatch() {
        if (artworkQueue.length) {
            scheduleArtworkFlush();
        }
    }

    function flushArtworkQueue() {
        var base;
        var batch;

        if (!artworkQueue.length) { return; }
        base = artworkServiceBase();
        if (!base || !window.BlazzingNet || !window.BlazzingNet.postJson) {
            while (artworkQueue.length) {
                finishArtworkTask(artworkQueue.shift(), "", false);
            }
            return;
        }

        batch = artworkQueue.splice(0, MAX_ARTWORK_BATCH);
        window.BlazzingNet.postJson(
            base + "/api/v1/artwork/resolve",
            artworkPayload(batch),
            { timeout: 18000, maxBytes: 256 * 1024 }
        ).then(function (result) {
            finishArtworkBatch(batch, result);
        }).catch(function () {
            failArtworkBatch(batch);
        }).then(scheduleNextArtworkBatch, scheduleNextArtworkBatch);
    }

    function resolveOnDemandArtwork(item) {
        var cacheKey;
        var requestId;

        if (!item || item.logo) {
            return Promise.resolve(item && item.logo || "");
        }
        if (item.kind !== "vod" && item.kind !== "series") {
            return Promise.resolve("");
        }

        cacheKey = artworkLookupKey(item);
        if (Object.prototype.hasOwnProperty.call(artworkSessionCache, cacheKey)) {
            item.logo = artworkSessionCache[cacheKey] || "";
            return Promise.resolve(item.logo);
        }
        if (artworkInFlight[cacheKey]) {
            return artworkInFlight[cacheKey];
        }

        requestId = "art-" + (++artworkSequence);
        artworkInFlight[cacheKey] = new Promise(function (resolve) {
            artworkQueue.push({
                cacheKey: cacheKey,
                requestId: requestId,
                item: item,
                resolve: resolve
            });
            scheduleArtworkFlush();
        });
        return artworkInFlight[cacheKey];
    }

    function cardShardDescriptor(item) {
        var base = String(item.cardIndexBase || "").replace(/\/$/, "");
        var key = String(item.cardKey || "");
        var version = String(item.cardIndexVersion || "");
        var prefixLength = parseInt(item.cardIndexShardLength || 1, 10);
        if (!(prefixLength >= 1 && prefixLength <= 4)) {
            prefixLength = 1;
        }
        var prefix = key.slice(0, prefixLength);
        return {
            base: base,
            key: key,
            version: version,
            prefix: prefix,
            cacheKey: base + "|" + version + "|" + prefix
        };
    }

    function cardShardRequestUrl(descriptor) {
        var url = descriptor.base + "/" + descriptor.prefix + ".json";
        if (descriptor.version) {
            url += "?v=" + encodeURIComponent(descriptor.version);
        }
        return url;
    }

    function cardShardPromise(descriptor) {
        var cacheKey = descriptor.cacheKey;
        if (!cardShardPromises[cacheKey]) {
            cardShardPromises[cacheKey] = queueCardShard(
                cardShardRequestUrl(descriptor)
            ).then(function (rows) {
                cardShardCache[cacheKey] =
                    rows && typeof rows === "object" ? rows : {};
                delete cardShardFailureAt[cacheKey];
                return cardShardCache[cacheKey];
            }).catch(function () {
                cardShardFailureAt[cacheKey] = Date.now();
                delete cardShardPromises[cacheKey];
                return {};
            });
        }
        return cardShardPromises[cacheKey];
    }

    function resolveCardLogo(item) {
        var descriptor;
        var rows;
        var failedAt;

        if (item.logo) {
            return Promise.resolve(item.logo);
        }

        descriptor = cardShardDescriptor(item);
        if (!descriptor.base || !descriptor.key) {
            return resolveOnDemandArtwork(item);
        }

        rows = cardShardCache[descriptor.cacheKey];
        if (rows) {
            item.logo = rows[descriptor.key] || "";
            return item.logo ? Promise.resolve(item.logo) :
                resolveOnDemandArtwork(item);
        }

        failedAt = cardShardFailureAt[descriptor.cacheKey] || 0;
        if (failedAt && Date.now() - failedAt < 30000) {
            return resolveOnDemandArtwork(item);
        }

        return cardShardPromise(descriptor).then(function (resolvedRows) {
            item.logo = resolvedRows[descriptor.key] || "";
            return item.logo || resolveOnDemandArtwork(item);
        });
    }

    function appendCardPosterImage(poster, item) {
        var imageUrl = safeImageUrl(item.logo);
        var image;

        if (!imageUrl && !item.cardKey &&
                item.kind !== "vod" && item.kind !== "series") {
            return;
        }

        image = document.createElement("img");
        image.alt = "";
        image.className = "poster-image";
        image.setAttribute("draggable", "false");
        image.setAttribute("referrerpolicy", "no-referrer");
        poster.appendChild(image);

        if (imageUrl) {
            observeImage(image, imageUrl);
            return;
        }

        resolveCardLogo(item).then(function (resolvedUrl) {
            resolvedUrl = safeImageUrl(resolvedUrl);
            if (resolvedUrl) { observeImage(image, resolvedUrl); }
        });
    }

    function makeCardPoster(item) {
        var poster = document.createElement("div");
        poster.className = "poster";
        poster.appendChild(posterFallback(item.name));
        appendCardPosterImage(poster, item);
        return poster;
    }

    function makeCardCopy(item) {
        var copy = document.createElement("div");
        var title = document.createElement("strong");
        var meta = document.createElement("small");

        copy.className = "card-copy";
        title.textContent = item.name || "Item";
        meta.textContent = item.categoryName ||
            item.group ||
            sectionTitle(state.kind);
        copy.appendChild(title);
        copy.appendChild(meta);
        return copy;
    }

    function bindCardMain(main, card, item) {
        main.addEventListener("click", function () {
            if (item.kind === "series") {
                openSeries(item);
            } else {
                playItem(item);
            }
        });
        main.addEventListener("focus", function () {
            card.classList.add("focused");
            state.returnFocusUid = item.uid || "";
            maybeAppendCards(main);
        });
        main.addEventListener("blur", function () {
            card.classList.remove("focused");
        });
    }

    function makeFavoriteMarker(item) {
        var favorite = document.createElement("span");
        favorite.className = "favorite" +
            (window.BlazzingStorage.isFavorite(item.uid) ? " on" : "");
        favorite.textContent = "★";
        favorite.setAttribute("aria-hidden", "true");
        favorite.setAttribute("data-item-uid", item.uid);
        /*
         * The star is display-only on TV. Making it a real button introduced
         * a second focus stop inside every card and caused Samsung remotes to
         * get trapped between the card and its favorite control. Favorites
         * are toggled with the yellow remote key instead.
         */
        return favorite;
    }

    function makeCard(item, index) {
        var card = document.createElement("article");
        var main = document.createElement("button");

        card.className = "media-card kind-" + (item.kind || "item");
        card.setAttribute("data-item-uid", item.uid);
        main.className = "card-main";
        main.setAttribute("data-focusable", "true");
        main.setAttribute("data-item-uid", item.uid);
        main.setAttribute("data-card-index", String(index));
        main.appendChild(makeCardPoster(item));
        main.appendChild(makeCardCopy(item));
        bindCardMain(main, card, item);
        card.appendChild(main);
        card.appendChild(makeFavoriteMarker(item));
        return card;
    }


    function catalogGrid() {
        return byId("catalog-grid");
    }

    function catalogViewport() {
        return catalogGrid().parentNode;
    }

    function nextGridRange() {
        var start = state.visibleCount;
        return {
            start: start,
            end: Math.min(state.filtered.length, start + state.batchSize)
        };
    }

    function appendCardRange(grid, range) {
        var fragment = document.createDocumentFragment();
        var i;

        for (i = range.start; i < range.end; i += 1) {
            fragment.appendChild(makeCard(state.filtered[i], i));
        }
        grid.appendChild(fragment);
    }

    function gridNeedsFill(content) {
        return state.visibleCount < state.filtered.length &&
            content.scrollHeight <= content.clientHeight + 500;
    }

    function gridNearEnd(content) {
        return content.scrollTop + content.clientHeight >=
            content.scrollHeight - 900;
    }

    function resetGridState(grid) {
        state.renderGeneration += 1;
        grid.innerHTML = "";
        state.visibleCount = 0;
        resetImagePipeline();
    }

    function updateGridEmptyState() {
        byId("catalog-empty").classList.toggle(
            "hidden",
            state.filtered.length !== 0
        );
    }


    function appendGridBatch() {
        var grid = catalogGrid();
        var range = nextGridRange();

        if (range.start >= range.end) { return; }

        appendCardRange(grid, range);
        state.visibleCount = range.end;
    }


    function ensureGridFilled() {
        var content = catalogViewport();
        var guard = 0;

        while (gridNeedsFill(content) && guard < 3) {
            appendGridBatch();
            guard += 1;
        }
    }


    function scheduleGridAppend() {
        if (gridAppendScheduled) { return; }
        gridAppendScheduled = true;

        (window.requestAnimationFrame || window.setTimeout)(function () {
            gridAppendScheduled = false;
            if (state.view === "catalog" && gridNearEnd(catalogViewport())) {
                appendGridBatch();
            }
        }, 16);
    }


    function renderGrid(reset) {
        var grid = catalogGrid();

        if (reset) { resetGridState(grid); }

        appendGridBatch();
        setTimeout(ensureGridFilled, 0);
        updateGridEmptyState();
    }


    function maybeAppendCards(element) {
        var index = parseInt(element.getAttribute("data-card-index"), 10);
        var nearTail = !isNaN(index) && index >= state.visibleCount - 10;

        if (nearTail && state.visibleCount < state.filtered.length) {
            appendGridBatch();
        }
    }

    function openSeries(series) {
        setBusy(true, "Carregando episódios…");

        if (series.source === "m3u") {
            loadStoredM3uCatalog(state.profile, "series", {
                onlyKind: "series",
                seriesFilterKey: series.seriesKey || series.name
            }).then(function (catalog) {
                var fullSeries = catalog && catalog.items && catalog.items[0];
                setBusy(false);
                showSeries({
                    title: series.name,
                    episodes: fullSeries && fullSeries.episodes ?
                        fullSeries.episodes : []
                });
            }).catch(function (error) {
                setBusy(false);
                showToast(error.message || "Falha ao carregar episódios.");
            });
            return;
        }

        state.xtream.seriesInfo(series).then(function (details) {
            setBusy(false);
            showSeries(details);
        }).catch(function (error) {
            setBusy(false);
            showToast(error.message || "Falha ao carregar episódios.");
        });
    }


    function activateSeries(details) {
        state.series = details;
        state.currentSeason = "all";
        byId("series-title").textContent = details.title || "Série";
        setView("series");
    }

    function episodeSeasonValue(episode) {
        return parseInt(String(episode.season || 0), 10) || 0;
    }

    function setSeriesSeason(season) {
        state.currentSeason = season;
        renderSeasons();
        renderEpisodes();
    }

    function makeSeasonButton(season, label) {
        var value = String(season);
        var button = document.createElement("button");

        button.textContent = label;
        button.setAttribute("data-focusable", "true");
        button.className =
            String(state.currentSeason) === value ? "active" : "";
        button.addEventListener("click", function () {
            setSeriesSeason(value);
        });
        return button;
    }

    function visibleSeriesEpisodes() {
        return (state.series.episodes || []).filter(function (episode) {
            return state.currentSeason === "all" ||
                String(episode.season) === String(state.currentSeason);
        });
    }

    function makeEpisodeButton(episode) {
        var button = document.createElement("button");
        var title = document.createElement("strong");
        var meta = document.createElement("small");

        button.className = "episode-card";
        button.setAttribute("data-focusable", "true");
        button.setAttribute("data-item-uid", episode.uid);
        title.textContent = episode.name || ("Episódio " + episode.episode);
        meta.textContent = "T" + episode.season + " · E" + episode.episode;
        button.appendChild(title);
        button.appendChild(meta);
        button.addEventListener("click", function () { playItem(episode); });
        return button;
    }


    function showSeries(details) {
        activateSeries(details);
        renderSeasons();
        renderEpisodes();
    }


    function seriesSeasons() {
        var seen = {};
        var seasons = [];

        (state.series.episodes || []).forEach(function (episode) {
            var season = episodeSeasonValue(episode);
            var key = String(season);
            if (seen[key]) { return; }
            seen[key] = true;
            seasons.push(season);
        });

        seasons.sort(function (a, b) { return a - b; });
        return seasons;
    }


    function renderSeasons() {
        var root = byId("season-list");

        root.innerHTML = "";
        root.appendChild(makeSeasonButton("all", "Todos"));
        seriesSeasons().forEach(function (season) {
            root.appendChild(makeSeasonButton(
                season,
                "Temporada " + season
            ));
        });
    }


    function renderEpisodes() {
        var root = byId("episode-grid");
        var episodes = visibleSeriesEpisodes();

        root.innerHTML = "";
        byId("episodes-empty").classList.toggle("hidden", episodes.length !== 0);
        episodes.forEach(function (episode) {
            root.appendChild(makeEpisodeButton(episode));
        });
    }

    byId("series-back").addEventListener("click", function () {
        setView("catalog");
    });


    function resumePosition(item) {
        return item.kind === "live" ?
            0 : window.BlazzingStorage.getProgress(item.uid);
    }

    function rememberPlayerOrigin(item) {
        state.playerReturnView =
            state.view === "series" ? "series" : "catalog";
        state.currentPlaylistIndex = state.filtered.indexOf(item);
        state.returnFocusUid = item.uid || "";

        if (state.view === "catalog") {
            state.catalogScrollTop = catalogViewport().scrollTop || 0;
        }
    }

    function setPlayerHeader(title, status) {
        byId("player-title").textContent = title;
        byId("player-status").textContent = status;
    }

    function catalogReturnTarget() {
        var candidates = catalogGrid().querySelectorAll("[data-item-uid]");
        var i;
        for (i = 0; i < candidates.length; i += 1) {
            if (candidates[i].getAttribute("data-item-uid") === state.returnFocusUid &&
                    candidates[i].classList.contains("card-main")) {
                return candidates[i];
            }
        }
        return null;
    }

    function saveCurrentProgress() {
        var item = window.BlazzingPlayer.item();
        if (item && item.kind !== "live") {
            window.BlazzingStorage.setProgress(
                item.uid,
                window.BlazzingPlayer.currentTime()
            );
        }
    }

    function currentLiveIndex(list, item) {
        var index = list.indexOf(item);
        if (index >= 0) { return index; }
        return state.currentPlaylistIndex >= 0 ?
            state.currentPlaylistIndex : 0;
    }

    function openLiveAt(index) {
        var item = state.filtered[index];
        state.currentPlaylistIndex = index;
        state.returnFocusUid = item.uid || state.returnFocusUid;
        setPlayerHeader(
            item.name || "Canal",
            "Canal " + (index + 1) + " de " +
                state.filtered.length + " · Preparando…"
        );
        window.BlazzingPlayer.open(item, 0);
    }

    function dimPlayerHudLater() {
        hudTimer = setTimeout(function () {
            if (state.view === "player") {
                byId("player-hud").classList.add("dim");
            }
        }, 4500);
    }

    function persistPlaybackTick(milliseconds) {
        var item = window.BlazzingPlayer.item();
        var now = Date.now();

        if (!item || item.kind === "live" ||
                now - state.lastProgressWrite <= 10000) {
            return;
        }
        state.lastProgressWrite = now;
        window.BlazzingStorage.setProgress(item.uid, milliseconds);
    }


    function playItem(item) {
        var resume = resumePosition(item);

        rememberPlayerOrigin(item);
        setPlayerHeader(item.name || "Reproduzindo", "Preparando…");
        setView("player");
        window.BlazzingPlayer.open(item, resume);
        showHud();
    }


    function restorePlayerReturnFocus() {
        var target;

        if (state.playerReturnView !== "catalog") { return; }

        try {
            catalogViewport().scrollTop = state.catalogScrollTop || 0;
        } catch (ignoreScroll) {}

        target = catalogReturnTarget();
        if (!target) { return; }

        target.focus();
        try { target.scrollIntoView(false); } catch (ignoreFocusScroll) {}
    }


    function closePlayer() {
        saveCurrentProgress();
        window.BlazzingPlayer.stop();
        setView(state.playerReturnView || "catalog");
        setTimeout(restorePlayerReturnFocus, 40);
    }


    function switchLive(delta) {
        var list = state.filtered;
        var index;
        var now = Date.now();

        if (state.kind !== "live" || !list.length ||
                now - state.lastZapAt < 280) {
            return;
        }

        state.lastZapAt = now;
        index = currentLiveIndex(list, window.BlazzingPlayer.item());
        index = (index + delta + list.length) % list.length;
        openLiveAt(index);
    }


    function showHud() {
        clearTimeout(hudTimer);
        byId("player-hud").classList.remove("dim");
        dimPlayerHudLater();
    }

    window.BlazzingPlayer.setStateListener(function (text) {
        byId("player-status").textContent = text;
        showHud();
    });

    window.BlazzingPlayer.setTimeListener(persistPlaybackTick);

    byId("catalog-grid").parentNode.addEventListener("scroll", scheduleGridAppend);

    document.addEventListener("mousemove", function () {
        if (state.view === "player") {
            showHud();
        }
    });

    window.addEventListener("beforeunload", function () {
        if (window.BlazzingPairing) { window.BlazzingPairing.stop(); }
        if (window.BlazzingPlayer) { window.BlazzingPlayer.stop(); }
    });

    function restoreLastOpenedPlaylist() {
        var lastId = window.BlazzingStorage.lastOpenedProfileId();
        var profiles;
        var i;
        var profile;

        if (!lastId) {
            return false;
        }

        profiles = window.BlazzingStorage.profiles();
        for (i = 0; i < profiles.length; i += 1) {
            if (profiles[i].id === lastId && profiles[i].type === "m3u") {
                profile = profiles[i];
                break;
            }
        }

        if (!profile) {
            window.BlazzingStorage.setLastOpenedProfileId("");
            return false;
        }

        setMode("m3u");
        byId("profile-name").value = profile.name || "";
        byId("m3u-url").value = profile.url || "";

        if (realTizenDevice()) {
            if (localFlag(AUTO_OPEN_IN_PROGRESS_KEY)) {
                setLocalFlag(AUTO_OPEN_IN_PROGRESS_KEY, "");
                setLocalFlag(AUTO_OPEN_SUPPRESSED_KEY, "1");
                showToast("A abertura automática anterior falhou. Abra a lista manualmente.");
                return false;
            }
            if (localFlag(AUTO_OPEN_SUPPRESSED_KEY)) {
                return false;
            }
            setLocalFlag(AUTO_OPEN_IN_PROGRESS_KEY, "1");
        }

        connectM3u(profile, false, "live");
        return true;
    }

    registerRemoteKeys();
    renderSavedProfiles();
    setMode("m3u");
    if (!restoreLastOpenedPlaylist()) {
        focusFirst();
    }

    if (window.BlazzingBoot) { window.BlazzingBoot.markReady(); }

    if (window.BlazzingWindowsNative) {
        byId("platform-badge").textContent = "Windows · mpv";
    } else if (window.webapis && window.webapis.avplay) {
        byId("platform-badge").textContent = "Tizen · AVPlay";
    } else {
        byId("platform-badge").textContent = "Browser · HTML5 fallback";
    }
}());
