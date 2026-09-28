/* SPDX-License-Identifier: MIT */
/* Main controller for the Blazzing Samsung Tizen Web application. */
(function () {
    "use strict";

    var byId = function (id) { return document.getElementById(id); };
    var state = {
        mode: "m3u",
        view: "home",
        kind: "live",
        profile: null,
        xtream: null,
        m3uCatalogs: null,
        catalog: { items: [], categories: [] },
        filtered: [],
        category: "all",
        favoritesOnly: false,
        visibleCount: 0,
        batchSize: 30,
        renderGeneration: 0,
        series: null,
        currentSeason: "all",
        playerReturnView: "catalog",
        currentPlaylistIndex: -1,
        lastProgressWrite: 0,
        pairingActive: false,
        returnFocusUid: "",
        catalogScrollTop: 0,
        lastZapAt: 0
    };

    var toastTimer = 0;
    var hudTimer = 0;
    var supportedKeys = {};
    var searchTimer = 0;
    var gridAppendScheduled = false;
    var imageObserver = null;
    var imageQueue = [];
    var activeImageLoads = 0;
    var MAX_IMAGE_LOADS = 6;


    function showToast(message) {
        clearTimeout(toastTimer);
        byId("toast").textContent = message;
        byId("toast").classList.remove("hidden");
        toastTimer = setTimeout(function () {
            byId("toast").classList.add("hidden");
        }, 3200);
    }

    function setBusy(active, message) {
        byId("busy-text").textContent = message || "Carregando…";
        byId("busy").classList.toggle("hidden", !active);
    }

    function setView(name) {
        ["home", "catalog", "series", "player"].forEach(function (view) {
            byId(view + "-view").classList.toggle("hidden", view !== name);
        });
        state.view = name;
        setTimeout(focusFirst, 0);
    }

    function focusables() {
        var root = state.pairingActive ? byId("pairing-modal") : byId(state.view + "-view");
        var all;

        if (!root) {
            return [];
        }

        all = Array.prototype.slice.call(
            root.querySelectorAll('[data-focusable="true"]:not(.hidden)')
        );

        var activeIndex = -1;
        var active = document.activeElement;
        if (state.view === "catalog" && active &&
                active.hasAttribute("data-card-index")) {
            activeIndex = parseInt(active.getAttribute("data-card-index"), 10);
        }

        return all.filter(function (element) {
            var cardIndex;
            var rect;

            if (element.disabled) {
                return false;
            }

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
        });
    }

    function focusFirst() {
        var items = focusables();
        if (items.length) {
            items[0].focus();
        }
    }

function elementCenter(element) {
        var rect = element.getBoundingClientRect();
        return {
            x: rect.left + rect.width / 2,
            y: rect.top + rect.height / 2
        };
    }

    function moveAllowed(direction, dx, dy) {
        if (direction === "left") {
            return dx < -4;
        }
        if (direction === "right") {
            return dx > 4;
        }
        if (direction === "up") {
            return dy < -4;
        }
        return dy > 4;
    }

    function directionalScore(direction, dx, dy) {
        var horizontal = direction === "left" || direction === "right";
        var primary = horizontal ? Math.abs(dx) : Math.abs(dy);
        var secondary = horizontal ? Math.abs(dy) : Math.abs(dx);
        return primary + secondary * 2.4;
    }

    function bestDirectionalCandidate(items, current, direction) {
        var origin = elementCenter(current);
        var best = null;
        var bestScore = Infinity;

        items.forEach(function (candidate) {
            if (candidate === current) {
                return;
            }

            var point = elementCenter(candidate);
            var dx = point.x - origin.x;
            var dy = point.y - origin.y;
            if (!moveAllowed(direction, dx, dy)) {
                return;
            }

            var score = directionalScore(direction, dx, dy);
            if (score < bestScore) {
                bestScore = score;
                best = candidate;
            }
        });

        return best;
    }

    function focusDirectionalCandidate(candidate) {
        if (!candidate) {
            return;
        }

        candidate.focus();
        try {
            candidate.scrollIntoView(false);
        } catch (ignoreScroll) {}
        maybeAppendCards(candidate);
    }

    function geometricMove(direction) {
        var current = document.activeElement;
        var items = focusables();

        if (!current || items.indexOf(current) === -1) {
            focusFirst();
            return;
        }

        focusDirectionalCandidate(
            bestDirectionalCandidate(items, current, direction)
        );
    }

    function registerRemoteKeys() {
        var names = [
            "MediaPlay",
            "MediaPause",
            "MediaStop",
            "MediaPlayPause",
            "MediaFastForward",
            "MediaRewind"
        ];

        if (!window.tizen || !window.tizen.tvinputdevice) {
            return;
        }

        try {
            window.tizen.tvinputdevice.getSupportedKeys().forEach(function (key) {
                supportedKeys[key.code] = key.name;
            });
        } catch (ignoreSupported) {}

        try {
            if (window.tizen.tvinputdevice.registerKeyBatch) {
                window.tizen.tvinputdevice.registerKeyBatch(names);
            } else {
                names.forEach(function (name) {
                    try { window.tizen.tvinputdevice.registerKey(name); }
                    catch (ignoreKey) {}
                });
            }
        } catch (ignoreBatch) {}
    }

    function exitApplication() {
        if (window.tizen && window.tizen.application) {
            try {
                window.tizen.application.getCurrentApplication().exit();
                return;
            } catch (ignoreExit) {}
        }
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
            setView("home");
        } else {
            exitApplication();
        }
    }

    function isBackKey(name, keyCode) {
        return name === "Back" || keyCode === 10009 || keyCode === 27;
    }

    function isPlaybackToggleKey(name, keyCode) {
        return name === "MediaPlayPause" || name === "MediaPlay" ||
            name === "MediaPause" || keyCode === 13 || keyCode === 32 ||
            keyCode === 415 || keyCode === 19;
    }

    function playerItemIsLive() {
        var item = window.BlazzingPlayer.item();
        return !!item && item.kind === "live";
    }

    function seekOrZap(seconds, liveDelta) {
        if (playerItemIsLive()) {
            switchLive(liveDelta);
        } else {
            window.BlazzingPlayer.seek(seconds);
        }
        showHud();
    }

    function handlePlayerKey(name, keyCode) {
        if (isBackKey(name, keyCode)) {
            goBack();
            return true;
        }
        if (isPlaybackToggleKey(name, keyCode)) {
            window.BlazzingPlayer.togglePause();
            showHud();
            return true;
        }
        if (name === "MediaStop" || keyCode === 413) {
            closePlayer();
            return true;
        }
        if (keyCode === 37 || name === "MediaRewind" || keyCode === 412) {
            seekOrZap(-10, -1);
            return true;
        }
        if (keyCode === 39 || name === "MediaFastForward" || keyCode === 417) {
            seekOrZap(10, 1);
            return true;
        }
        showHud();
        return false;
    }

    function handleNavigationKey(event, code, name, active, isInput) {
        if (isBackKey(name, code)) {
            event.preventDefault();
            goBack();
            return true;
        }
        if (code === 13 && !isInput) {
            if (active && active.click) {
                event.preventDefault();
                active.click();
            }
            return true;
        }
        if (isInput && (code === 37 || code === 39)) {
            return true;
        }
        if (code !== 37 && code !== 38 && code !== 39 && code !== 40) {
            return false;
        }
        event.preventDefault();
        geometricMove(
            code === 37 ? "left" :
            code === 38 ? "up" :
            code === 39 ? "right" : "down"
        );
        return true;
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
        handleNavigationKey(event, code, name, active, isInput);
    });

    function setMode(mode) {
        state.mode = mode;
        byId("mode-m3u").classList.toggle("active", mode === "m3u");
        byId("mode-xtream").classList.toggle("active", mode === "xtream");
        byId("m3u-fields").classList.toggle("hidden", mode !== "m3u");
        byId("xtream-fields").classList.toggle("hidden", mode !== "xtream");
        byId("save-secret-row").classList.toggle("hidden", mode !== "xtream");
    }

    byId("mode-m3u").addEventListener("click", function () {
        setMode("m3u");
    });

    byId("mode-xtream").addEventListener("click", function () {
        setMode("xtream");
    });

    function setPairingStatus(message, kind) {
        var target = byId("pairing-status");
        var retry = byId("pairing-retry");
        if (!target) { return; }
        target.textContent = message || "";
        target.setAttribute("data-state", kind || "");

        if (retry) {
            retry.classList.toggle("hidden", kind !== "expired");
        }
        if (kind === "expired") {
            byId("pairing-qr").innerHTML = "";
            byId("pairing-url").textContent = "";
            setTimeout(function () {
                if (state.pairingActive && retry) { retry.focus(); }
            }, 0);
        }
    }

    function cancelPairing() {
        if (window.BlazzingPairing) {
            window.BlazzingPairing.stop();
        }
        state.pairingActive = false;
        byId("pairing-modal").classList.add("hidden");
        setTimeout(focusFirst, 0);
    }

    function acceptPairedPlaylist(profile) {
        var normalized = {
            type: "m3u",
            name: profile.name || "Lista do celular",
            url: profile.url
        };

        state.pairingActive = false;
        byId("pairing-modal").classList.add("hidden");
        setMode("m3u");
        byId("profile-name").value = normalized.name;
        byId("m3u-url").value = normalized.url;
        byId("save-profile").checked = true;
        connectM3u(normalized);
    }

    function startPairing() {
        if (!window.BlazzingPairing) {
            showToast("O módulo de pareamento não foi carregado.");
            return;
        }

        state.pairingActive = true;
        byId("pairing-modal").classList.remove("hidden");
        byId("pairing-qr").innerHTML = "";
        byId("pairing-url").textContent = "";
        setPairingStatus("Preparando sessão segura…", "creating");
        setTimeout(focusFirst, 0);

        window.BlazzingPairing.start(
            acceptPairedPlaylist,
            setPairingStatus
        ).then(function (session) {
            if (!state.pairingActive) {
                return;
            }
            byId("pairing-qr").innerHTML = session.qrSvg;
            byId("pairing-url").textContent = session.url;
        }).catch(function (error) {
            if (!state.pairingActive) {
                return;
            }
            setPairingStatus(
                error.message || "Não foi possível iniciar o pareamento.",
                "error"
            );
        });
    }

    byId("pair-button").addEventListener("click", startPairing);
    byId("pairing-retry").addEventListener("click", startPairing);
    byId("pairing-cancel").addEventListener("click", cancelPairing);

    function profileFromForm() {
        var name = byId("profile-name").value.replace(/^\s+|\s+$/g, "") ||
            "Minha lista";

        if (state.mode === "m3u") {
            return {
                type: "m3u",
                name: name,
                url: byId("m3u-url").value.replace(/^\s+|\s+$/g, "")
            };
        }

        return {
            type: "xtream",
            name: name,
            server: byId("xtream-server").value.replace(/^\s+|\s+$/g, ""),
            alternate: byId("xtream-alternate").value.replace(/^\s+|\s+$/g, ""),
            username: byId("xtream-user").value,
            password: byId("xtream-password").value
        };
    }

    function saveProfileIfRequested(profile) {
        var copy;

        if (!byId("save-profile").checked) {
            return;
        }

        copy = JSON.parse(JSON.stringify(profile));
        if (copy.type === "xtream" && !byId("save-secret").checked) {
            copy.password = "";
        }

        window.BlazzingStorage.saveProfile(copy);
        renderSavedProfiles();
    }

    function connectM3u(profile) {
        if (!/^https?:\/\//i.test(profile.url)) {
            showToast("Informe uma URL M3U/M3U8 HTTP ou HTTPS.");
            return;
        }

        setBusy(true, "Baixando playlist…");

        window.BlazzingProviders.loadM3u(profile.url).then(function (catalogs) {
            state.profile = profile;
            state.m3uCatalogs = catalogs;
            state.xtream = null;
            saveProfileIfRequested(profile);
            setBusy(false);
            openCatalog("live");
        }).catch(function (error) {
            setBusy(false);
            showToast(error.message || "Falha ao carregar a playlist.");
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
            state.profile = profile;
            state.xtream = client;
            state.m3uCatalogs = null;
            saveProfileIfRequested(profile);
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
        var profile;

        event.preventDefault();
        profile = profileFromForm();

        if (profile.type === "m3u") {
            connectM3u(profile);
        } else {
            connectXtream(profile);
        }
    });

    function loadProfile(profile) {
        setMode(profile.type);
        byId("profile-name").value = profile.name || "";

        if (profile.type === "m3u") {
            byId("m3u-url").value = profile.url || "";
            connectM3u(profile);
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
            var card = document.createElement("div");
            var info = document.createElement("div");
            var name = document.createElement("strong");
            var meta = document.createElement("small");
            var actions = document.createElement("div");
            var open = document.createElement("button");
            var remove = document.createElement("button");

            card.className = "saved-card";
            name.textContent = profile.name || "Perfil";
            meta.textContent = profile.type === "m3u" ?
                "M3U / M3U8" : "Xtream Codes";

            info.appendChild(name);
            info.appendChild(meta);

            actions.className = "saved-actions";

            open.textContent = "Abrir";
            open.setAttribute("data-focusable", "true");
            open.addEventListener("click", function () {
                loadProfile(profile);
            });

            remove.textContent = "Excluir";
            remove.setAttribute("data-focusable", "true");
            remove.addEventListener("click", function () {
                window.BlazzingStorage.removeProfile(profile.id);
                renderSavedProfiles();
                focusFirst();
            });

            actions.appendChild(open);
            actions.appendChild(remove);
            card.appendChild(info);
            card.appendChild(actions);
            root.appendChild(card);
        });
    }

    function sectionTitle(kind) {
        if (kind === "vod") { return "Filmes"; }
        if (kind === "series") { return "Séries"; }
        return "TV ao vivo";
    }

    function loadCatalog(kind) {
        state.kind = kind;
        state.category = "all";
        state.favoritesOnly = false;
        byId("favorites-button").classList.remove("active");

        if (state.m3uCatalogs) {
            state.catalog = state.m3uCatalogs[kind] ||
                { items: [], categories: [] };
            return Promise.resolve(state.catalog);
        }

        if (!state.xtream) {
            return Promise.reject(new Error("Nenhum provider carregado."));
        }

        setBusy(true, "Carregando " + sectionTitle(kind).toLowerCase() + "…");

        return state.xtream.load(kind).then(function (catalog) {
            state.catalog = catalog;
            setBusy(false);
            return catalog;
        }).catch(function (error) {
            setBusy(false);
            throw error;
        });
    }

    function openCatalog(kind) {
        loadCatalog(kind).then(function () {
            setView("catalog");
            renderCatalog();
        }).catch(function (error) {
            showToast(error.message || "Falha ao carregar catálogo.");
        });
    }

    Array.prototype.forEach.call(
        document.querySelectorAll("[data-section]"),
        function (button) {
            button.addEventListener("click", function () {
                openCatalog(button.getAttribute("data-section"));
            });
        }
    );

    byId("favorites-button").addEventListener("click", function () {
        state.favoritesOnly = !state.favoritesOnly;
        byId("favorites-button").classList.toggle(
            "active",
            state.favoritesOnly
        );
        applyFilters();
    });

    byId("lists-button").addEventListener("click", function () {
        setView("home");
        renderSavedProfiles();
    });

    byId("catalog-search").addEventListener("input", function () {
        clearTimeout(searchTimer);
        searchTimer = setTimeout(applyFilters, 180);
    });

    function categoryButtons() {
        return byId("categories").querySelectorAll("[data-category-id]");
    }

    function eachCategoryButton(callback) {
        Array.prototype.forEach.call(categoryButtons(), callback);
    }

    function focusCategoryButton(id) {
        var target = null;
        eachCategoryButton(function (button) {
            if (!target && button.getAttribute("data-category-id") === id) {
                target = button;
            }
        });
        if (target) { target.focus(); }
    }

    function updateCategorySelection() {
        eachCategoryButton(function (button) {
            button.classList.toggle(
                "active",
                button.getAttribute("data-category-id") === state.category
            );
        });
    }

    function selectCategory(id) {
        state.category = id;
        updateCategorySelection();
        applyFilters();
        setTimeout(function () { focusCategoryButton(id); }, 0);
    }

    function categoryCounts() {
        return state.catalog.items.reduce(function (counts, item) {
            counts[item.categoryId] = (counts[item.categoryId] || 0) + 1;
            return counts;
        }, {});
    }

    function categoryButton(id, label, selected) {
        var button = document.createElement("button");
        button.textContent = label;
        button.setAttribute("data-focusable", "true");
        button.setAttribute("data-category-id", id);
        button.className = selected ? "active" : "";
        button.addEventListener("click", function () {
            selectCategory(id);
        });
        return button;
    }

    function renderCategories() {
        var root = byId("categories");
        var counts = categoryCounts();
        var fragment = document.createDocumentFragment();

        fragment.appendChild(categoryButton(
            "all",
            "Todos (" + state.catalog.items.length + ")",
            state.category === "all"
        ));

        state.catalog.categories.forEach(function (category) {
            var id = category.id;
            var name = category.name || "Outros";
            fragment.appendChild(categoryButton(
                id,
                name + " (" + (counts[id] || 0) + ")",
                state.category === id
            ));
        });

        root.innerHTML = "";
        root.appendChild(fragment);
    }

    function searchableText(item, normalize) {
        if (!item._searchText) {
            item._searchText = normalize([
                String(item.name || ""),
                String(item.categoryName || item.group || "")
            ].join(" "));
        }
        return item._searchText;
    }

    function catalogNormalizer() {
        return window.BlazzingProviders.normalizeWords || function (value) {
            return String(value || "").toLowerCase();
        };
    }

    function itemPassesFilters(item, query, favorites, normalize) {
        if (state.category !== "all" && item.categoryId !== state.category) {
            return false;
        }
        if (query && searchableText(item, normalize).indexOf(query) === -1) {
            return false;
        }
        return !state.favoritesOnly || !!favorites[item.uid];
    }

    function applyFilters() {
        var normalize = catalogNormalizer();
        var query = normalize(byId("catalog-search").value);
        var favorites = window.BlazzingStorage.favorites();

        state.filtered = state.catalog.items.filter(function (item) {
            return itemPassesFilters(item, query, favorites, normalize);
        });

        renderGrid(true);
        byId("item-count").textContent = state.filtered.length + " itens";
    }

    function updateSectionSelection() {
        Array.prototype.forEach.call(
            document.querySelectorAll("[data-section]"),
            function (button) {
                button.classList.toggle(
                    "active",
                    button.getAttribute("data-section") === state.kind
                );
            }
        );
    }

    function renderCatalog() {
        updateSectionSelection();
        byId("content-title").textContent = sectionTitle(state.kind);
        byId("content-eyebrow").textContent = state.profile ?
            state.profile.name : "Catálogo";
        byId("catalog-search").value = "";
        renderCategories();
        applyFilters();
    }

    function safeImageUrl(url) {
        var value = String(url || "").replace(/^\s+|\s+$/g, "");
        if (/^https?:\/\//i.test(value)) { return value; }
        if (/^\/\//.test(value)) { return "https:" + value; }
        return "";
    }

    function posterFallback(name) {
        var span = document.createElement("span");
        span.className = "poster-fallback";
        span.textContent = String(name || "?").charAt(0).toUpperCase();
        return span;
    }

    function imageTaskIsCurrent(task) {
        return !!task.image && !!task.image.parentNode &&
            task.generation === state.renderGeneration;
    }

    function finishImageTask(task, success) {
        if (task.finished) { return; }
        task.finished = true;

        if (task.timeout) {
            clearTimeout(task.timeout);
            task.timeout = 0;
        }
        activeImageLoads = Math.max(0, activeImageLoads - 1);

        if (imageTaskIsCurrent(task)) {
            task.image.classList.toggle("loaded", !!success);
            task.image.classList.toggle("failed", !success);
            if (!success) {
                try { task.image.parentNode.removeChild(task.image); }
                catch (ignoreRemove) {}
            }
        }
        setTimeout(pumpImageQueue, 0);
    }

    function startImageTask(task) {
        if (!imageTaskIsCurrent(task)) { return; }

        activeImageLoads += 1;
        task.finished = false;
        task.image.onload = function () { finishImageTask(task, true); };
        task.image.onerror = function () { finishImageTask(task, false); };
        task.timeout = setTimeout(function () {
            if (task.finished) { return; }
            try { task.image.src = ""; } catch (ignoreAbort) {}
            finishImageTask(task, false);
        }, 12000);
        task.image.src = task.url;
    }

    function pumpImageQueue() {
        while (activeImageLoads < MAX_IMAGE_LOADS && imageQueue.length) {
            startImageTask(imageQueue.shift());
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
            timeout: 0,
            finished: false
        });
        pumpImageQueue();
    }

    function observeImage(image, url) {
        image.setAttribute("data-src", url);
        if (imageObserver) {
            imageObserver.observe(image);
            return;
        }
        enqueueImage(image, url);
    }

    function enqueueVisibleImages(entries) {
        entries.forEach(function (entry) {
            if (!entry.isIntersecting) { return; }
            var image = entry.target;
            var url = image.getAttribute("data-src") || "";
            try { imageObserver.unobserve(image); } catch (ignoreUnobserve) {}
            enqueueImage(image, url);
        });
    }

    function resetImagePipeline() {
        imageQueue = [];
        activeImageLoads = 0;

        if (imageObserver) {
            try { imageObserver.disconnect(); } catch (ignoreDisconnect) {}
        }

        imageObserver = window.IntersectionObserver ?
            new IntersectionObserver(enqueueVisibleImages, {
                root: byId("catalog-grid").parentNode,
                rootMargin: "650px 0px",
                threshold: 0.01
            }) : null;
    }

    function makeCard(item, index) {
        var card = document.createElement("article");
        var main = document.createElement("button");
        var poster = document.createElement("div");
        var copy = document.createElement("div");
        var title = document.createElement("strong");
        var meta = document.createElement("small");
        var favorite = document.createElement("button");
        var imageUrl = safeImageUrl(item.logo);
        var fallback = posterFallback(item.name);

        card.className = "media-card kind-" + (item.kind || "item");
        card.setAttribute("data-item-uid", item.uid);

        main.className = "card-main";
        main.setAttribute("data-focusable", "true");
        main.setAttribute("data-item-uid", item.uid);
        main.setAttribute("data-card-index", String(index));

        poster.className = "poster";
        poster.appendChild(fallback);

        if (imageUrl) {
            var image = document.createElement("img");
            image.alt = "";
            image.className = "poster-image";
            image.setAttribute("draggable", "false");
            image.setAttribute("referrerpolicy", "no-referrer");
            poster.appendChild(image);
            observeImage(image, imageUrl);
        }

        copy.className = "card-copy";
        title.textContent = item.name || "Item";
        meta.textContent = item.categoryName ||
            item.group ||
            sectionTitle(state.kind);

        copy.appendChild(title);
        copy.appendChild(meta);
        main.appendChild(poster);
        main.appendChild(copy);

        main.addEventListener("click", function () {
            if (item.kind === "series") {
                openSeries(item);
            } else {
                playItem(item);
            }
        });

        favorite.className = "favorite" +
            (window.BlazzingStorage.isFavorite(item.uid) ? " on" : "");
        favorite.textContent = "★";
        favorite.setAttribute("aria-label", "Favorito");
        favorite.setAttribute("data-focusable", "true");
        favorite.setAttribute("data-item-uid", item.uid);
        favorite.setAttribute("data-card-index", String(index));

        favorite.addEventListener("click", function (event) {
            var on;
            event.stopPropagation();
            on = window.BlazzingStorage.toggleFavorite(item.uid);
            favorite.classList.toggle("on", on);

            if (state.favoritesOnly && !on) {
                applyFilters();
            }
        });

        card.appendChild(main);
        card.appendChild(favorite);
        return card;
    }

    function appendGridBatch() {
        var grid = byId("catalog-grid");
        var start = state.visibleCount;
        var end = Math.min(state.filtered.length, start + state.batchSize);
        var fragment;
        var i;

        if (start >= end) { return; }

        fragment = document.createDocumentFragment();
        for (i = start; i < end; i += 1) {
            fragment.appendChild(makeCard(state.filtered[i], i));
        }

        grid.appendChild(fragment);
        state.visibleCount = end;
    }

    function ensureGridFilled() {
        var content = byId("catalog-grid").parentNode;
        var guard = 0;

        while (state.visibleCount < state.filtered.length &&
                content.scrollHeight <= content.clientHeight + 500 &&
                guard < 3) {
            appendGridBatch();
            guard += 1;
        }
    }

    function scheduleGridAppend() {
        if (gridAppendScheduled) { return; }
        gridAppendScheduled = true;

        (window.requestAnimationFrame || window.setTimeout)(function () {
            var content = byId("catalog-grid").parentNode;
            gridAppendScheduled = false;

            if (state.view !== "catalog") { return; }

            if (content.scrollTop + content.clientHeight >=
                    content.scrollHeight - 900) {
                appendGridBatch();
            }
        }, 16);
    }

    function renderGrid(reset) {
        var grid = byId("catalog-grid");

        if (reset) {
            state.renderGeneration += 1;
            grid.innerHTML = "";
            state.visibleCount = 0;
            resetImagePipeline();
        }

        appendGridBatch();
        setTimeout(ensureGridFilled, 0);

        byId("catalog-empty").classList.toggle(
            "hidden",
            state.filtered.length !== 0
        );
    }

    function maybeAppendCards(element) {
        var index = parseInt(element.getAttribute("data-card-index"), 10);

        if (!isNaN(index) &&
                state.visibleCount < state.filtered.length &&
                index >= state.visibleCount - 10) {
            appendGridBatch();
        }
    }


    function openSeries(series) {
        setBusy(true, "Carregando episódios…");

        if (series.source === "m3u") {
            setBusy(false);
            showSeries({
                title: series.name,
                episodes: series.episodes || []
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

    function showSeries(details) {
        state.series = details;
        state.currentSeason = "all";
        byId("series-title").textContent = details.title || "Série";
        setView("series");
        renderSeasons();
        renderEpisodes();
    }

    function seriesSeasons() {
        var seen = {};
        var seasons = [];

        (state.series.episodes || []).forEach(function (episode) {
            var value = String(episode.season || 0);
            if (!seen[value]) {
                seen[value] = true;
                seasons.push(parseInt(value, 10) || 0);
            }
        });

        seasons.sort(function (a, b) { return a - b; });
        return seasons;
    }

    function renderSeasons() {
        var root = byId("season-list");
        var all = document.createElement("button");

        root.innerHTML = "";

        all.textContent = "Todos";
        all.setAttribute("data-focusable", "true");
        all.className = state.currentSeason === "all" ? "active" : "";
        all.addEventListener("click", function () {
            state.currentSeason = "all";
            renderSeasons();
            renderEpisodes();
        });
        root.appendChild(all);

        seriesSeasons().forEach(function (season) {
            var button = document.createElement("button");
            button.textContent = "Temporada " + season;
            button.setAttribute("data-focusable", "true");
            button.className =
                String(state.currentSeason) === String(season) ?
                    "active" : "";

            button.addEventListener("click", function () {
                state.currentSeason = String(season);
                renderSeasons();
                renderEpisodes();
            });

            root.appendChild(button);
        });
    }

    function renderEpisodes() {
        var root = byId("episode-grid");
        var episodes = (state.series.episodes || []).filter(function (episode) {
            return state.currentSeason === "all" ||
                String(episode.season) === String(state.currentSeason);
        });

        root.innerHTML = "";
        byId("episodes-empty").classList.toggle(
            "hidden",
            episodes.length !== 0
        );

        episodes.forEach(function (episode) {
            var button = document.createElement("button");
            var title = document.createElement("strong");
            var meta = document.createElement("small");

            button.className = "episode-card";
            button.setAttribute("data-focusable", "true");
            button.setAttribute("data-item-uid", episode.uid);

            title.textContent = episode.name ||
                ("Episódio " + episode.episode);
            meta.textContent =
                "T" + episode.season + " · E" + episode.episode;

            button.appendChild(title);
            button.appendChild(meta);

            button.addEventListener("click", function () {
                playItem(episode);
            });

            root.appendChild(button);
        });
    }

    byId("series-back").addEventListener("click", function () {
        setView("catalog");
    });

    function playItem(item) {
        var resume = item.kind === "live" ?
            0 : window.BlazzingStorage.getProgress(item.uid);

        state.playerReturnView =
            state.view === "series" ? "series" : "catalog";
        state.currentPlaylistIndex = state.filtered.indexOf(item);
        state.returnFocusUid = item.uid || "";

        if (state.view === "catalog") {
            state.catalogScrollTop = byId("catalog-grid").parentNode.scrollTop || 0;
        }

        byId("player-title").textContent = item.name || "Reproduzindo";
        byId("player-status").textContent = "Preparando…";

        setView("player");
        window.BlazzingPlayer.open(item, resume);
        showHud();
    }

    function restorePlayerReturnFocus() {
        var root;
        var candidates;
        var i;

        if (state.playerReturnView !== "catalog") {
            return;
        }

        try {
            byId("catalog-grid").parentNode.scrollTop = state.catalogScrollTop || 0;
        } catch (ignoreScroll) {}

        root = byId("catalog-grid");
        candidates = root.querySelectorAll("[data-item-uid]");
        for (i = 0; i < candidates.length; i += 1) {
            if (candidates[i].getAttribute("data-item-uid") === state.returnFocusUid &&
                    candidates[i].classList.contains("card-main")) {
                candidates[i].focus();
                try { candidates[i].scrollIntoView(false); } catch (ignoreFocusScroll) {}
                return;
            }
        }
    }

    function closePlayer() {
        var item = window.BlazzingPlayer.item();

        if (item && item.kind !== "live") {
            window.BlazzingStorage.setProgress(
                item.uid,
                window.BlazzingPlayer.currentTime()
            );
        }

        window.BlazzingPlayer.stop();
        setView(state.playerReturnView || "catalog");
        setTimeout(restorePlayerReturnFocus, 40);
    }

    function switchLive(delta) {
        var item;
        var list;
        var index;
        var now = Date.now();

        if (state.kind !== "live" || !state.filtered.length) {
            return;
        }

        if (now - state.lastZapAt < 280) {
            return;
        }
        state.lastZapAt = now;

        item = window.BlazzingPlayer.item();
        list = state.filtered;
        index = list.indexOf(item);

        if (index < 0) {
            index = state.currentPlaylistIndex >= 0 ?
                state.currentPlaylistIndex : 0;
        }

        index = (index + delta + list.length) % list.length;
        state.currentPlaylistIndex = index;
        item = list[index];

        byId("player-title").textContent = item.name || "Canal";
        byId("player-status").textContent =
            "Canal " + (index + 1) + " de " + list.length + " · Preparando…";
        state.returnFocusUid = item.uid || state.returnFocusUid;
        window.BlazzingPlayer.open(item, 0);
    }

    function showHud() {
        clearTimeout(hudTimer);
        byId("player-hud").classList.remove("dim");

        hudTimer = setTimeout(function () {
            if (state.view === "player") {
                byId("player-hud").classList.add("dim");
            }
        }, 4500);
    }

    window.BlazzingPlayer.setStateListener(function (text) {
        byId("player-status").textContent = text;
        showHud();
    });

    window.BlazzingPlayer.setTimeListener(function (milliseconds) {
        var item = window.BlazzingPlayer.item();
        var now = Date.now();

        if (item &&
                item.kind !== "live" &&
                now - state.lastProgressWrite > 10000) {
            state.lastProgressWrite = now;
            window.BlazzingStorage.setProgress(item.uid, milliseconds);
        }
    });

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

    registerRemoteKeys();
    renderSavedProfiles();
    setMode("m3u");
    focusFirst();

    if (window.BlazzingBoot) { window.BlazzingBoot.markReady(); }

    if (window.webapis && window.webapis.avplay) {
        byId("platform-badge").textContent = "Tizen · AVPlay";
    } else {
        byId("platform-badge").textContent = "Browser · HTML5 fallback";
    }
}());
