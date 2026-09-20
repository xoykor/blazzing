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
    var cardShardCache = {};
    var cardShardPromises = {};
    var cardShardFailureAt = {};
    var cardShardQueue = [];
    var activeCardShardLoads = 0;
    var MAX_CARD_SHARD_LOADS = 4;


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

    function geometricMove(direction) {
        var current = document.activeElement;
        var items = focusables();
        var currentRect;
        var cx;
        var cy;
        var best = null;
        var bestScore = Infinity;

        if (!current || items.indexOf(current) === -1) {
            focusFirst();
            return;
        }

        currentRect = current.getBoundingClientRect();
        cx = currentRect.left + currentRect.width / 2;
        cy = currentRect.top + currentRect.height / 2;

        items.forEach(function (candidate) {
            var rect;
            var x;
            var y;
            var dx;
            var dy;
            var primary;
            var secondary;
            var score;

            if (candidate === current) {
                return;
            }

            rect = candidate.getBoundingClientRect();
            x = rect.left + rect.width / 2;
            y = rect.top + rect.height / 2;
            dx = x - cx;
            dy = y - cy;

            if (direction === "left" && dx >= -4) { return; }
            if (direction === "right" && dx <= 4) { return; }
            if (direction === "up" && dy >= -4) { return; }
            if (direction === "down" && dy <= 4) { return; }

            primary = (direction === "left" || direction === "right") ?
                Math.abs(dx) : Math.abs(dy);
            secondary = (direction === "left" || direction === "right") ?
                Math.abs(dy) : Math.abs(dx);
            score = primary + secondary * 2.4;

            if (score < bestScore) {
                bestScore = score;
                best = candidate;
            }
        });

        if (best) {
            best.focus();
            try { best.scrollIntoView(false); } catch (ignoreScroll) {}
            maybeAppendCards(best);
        }
    }

    function registerRemoteKeys() {
        var names = [
            "MediaPlay",
            "MediaPause",
            "MediaStop",
            "MediaPlayPause",
            "MediaFastForward",
            "MediaRewind",
            "ColorF2Yellow"
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

    function handlePlayerKey(name, keyCode) {
        if (name === "Back" || keyCode === 10009 || keyCode === 27) {
            goBack();
            return true;
        }

        if (name === "MediaPlayPause" || name === "MediaPlay" ||
                name === "MediaPause" || keyCode === 13 ||
                keyCode === 32 || keyCode === 415 || keyCode === 19) {
            window.BlazzingPlayer.togglePause();
            showHud();
            return true;
        }

        if (name === "MediaStop" || keyCode === 413) {
            closePlayer();
            return true;
        }

        if (keyCode === 37 || name === "MediaRewind" || keyCode === 412) {
            if (window.BlazzingPlayer.item() &&
                    window.BlazzingPlayer.item().kind === "live") {
                switchLive(-1);
            } else {
                window.BlazzingPlayer.seek(-10);
            }
            showHud();
            return true;
        }

        if (keyCode === 39 || name === "MediaFastForward" || keyCode === 417) {
            if (window.BlazzingPlayer.item() &&
                    window.BlazzingPlayer.item().kind === "live") {
                switchLive(1);
            } else {
                window.BlazzingPlayer.seek(10);
            }
            showHud();
            return true;
        }

        showHud();
        return false;
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

    document.addEventListener("keydown", function (event) {
        var code = event.keyCode || event.which;
        var name = supportedKeys[code] || event.key || "";
        var active = document.activeElement;
        var isInput = active &&
            (active.tagName === "INPUT" || active.tagName === "TEXTAREA");

        if (state.view === "player") {
            if (handlePlayerKey(name, code)) {
                event.preventDefault();
            }
            return;
        }

        if (name === "Back" || code === 10009 || code === 27) {
            event.preventDefault();
            goBack();
            return;
        }

        if (state.view === "catalog" &&
                (name === "ColorF2Yellow" || code === 405)) {
            event.preventDefault();
            toggleFocusedFavorite();
            return;
        }

        if (code === 13 && !isInput) {
            if (active && active.click) {
                event.preventDefault();
                active.click();
            }
            return;
        }

        if (isInput && (code === 37 || code === 39)) {
            return;
        }

        if (code === 37 || code === 38 || code === 39 || code === 40) {
            event.preventDefault();
            geometricMove(
                code === 37 ? "left" :
                code === 38 ? "up" :
                code === 39 ? "right" : "down"
            );
        }
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

    function renderCategories() {
        var root = byId("categories");
        var all = document.createElement("button");
        var counts = {};

        state.catalog.items.forEach(function (item) {
            counts[item.categoryId] = (counts[item.categoryId] || 0) + 1;
        });

        root.innerHTML = "";

        all.textContent = "Todos (" + state.catalog.items.length + ")";
        all.setAttribute("data-focusable", "true");
        all.setAttribute("data-category-id", "all");
        all.className = state.category === "all" ? "active" : "";
        all.addEventListener("click", function () {
            state.category = "all";
            updateCategorySelection();
            applyFilters();
            setTimeout(function () { focusCategoryButton("all"); }, 0);
        });
        root.appendChild(all);

        state.catalog.categories.forEach(function (category) {
            var button = document.createElement("button");
            var count = counts[category.id] || 0;

            button.textContent = (category.name || "Outros") + " (" + count + ")";
            button.setAttribute("data-focusable", "true");
            button.setAttribute("data-category-id", category.id);
            button.className = state.category === category.id ? "active" : "";
            button.addEventListener("click", function () {
                state.category = category.id;
                updateCategorySelection();
                applyFilters();
                setTimeout(function () {
                    focusCategoryButton(category.id);
                }, 0);
            });
            root.appendChild(button);
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

                function done(success) {
                    if (finished) { return; }
                    finished = true;
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

                image.onload = function () { done(true); };
                image.onerror = function () { done(false); };

                /* Avoid keeping broken remote image requests alive forever. */
                task.timeout = setTimeout(function () {
                    if (!finished) {
                        try { image.src = ""; } catch (ignoreAbort) {}
                        done(false);
                    }
                }, 12000);

                var originalDone = done;
                done = function (success) {
                    clearTimeout(task.timeout);
                    originalDone(success);
                };

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

    function resolveCardLogo(item) {
        var base = String(item.cardIndexBase || "").replace(/\/$/, "");
        var key = String(item.cardKey || "");
        var version = String(item.cardIndexVersion || "");
        var prefix;
        var prefixLength;
        var cacheKey;
        var url;
        var failedAt;

        if (item.logo) {
            return Promise.resolve(item.logo);
        }
        if (!base || !key) {
            return Promise.resolve("");
        }

        prefixLength = parseInt(item.cardIndexShardLength || 1, 10);
        if (!(prefixLength >= 1 && prefixLength <= 4)) {
            prefixLength = 1;
        }
        prefix = key.slice(0, prefixLength);
        cacheKey = base + "|" + version + "|" + prefix;

        if (cardShardCache[cacheKey]) {
            item.logo = cardShardCache[cacheKey][key] || "";
            return Promise.resolve(item.logo);
        }

        failedAt = cardShardFailureAt[cacheKey] || 0;
        if (failedAt && Date.now() - failedAt < 30000) {
            return Promise.resolve("");
        }

        if (!cardShardPromises[cacheKey]) {
            url = base + "/" + prefix + ".json";
            if (version) {
                url += "?v=" + encodeURIComponent(version);
            }

            cardShardPromises[cacheKey] = queueCardShard(url)
            .then(function (rows) {
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

        return cardShardPromises[cacheKey].then(function (rows) {
            item.logo = rows[key] || "";
            return item.logo;
        });
    }

    function makeCard(item, index) {
        var card = document.createElement("article");
        var main = document.createElement("button");
        var poster = document.createElement("div");
        var copy = document.createElement("div");
        var title = document.createElement("strong");
        var meta = document.createElement("small");
        var favorite = document.createElement("span");
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

        if (imageUrl || item.cardKey) {
            var image = document.createElement("img");
            image.alt = "";
            image.className = "poster-image";
            image.setAttribute("draggable", "false");
            image.setAttribute("referrerpolicy", "no-referrer");
            poster.appendChild(image);

            if (imageUrl) {
                observeImage(image, imageUrl);
            } else {
                resolveCardLogo(item).then(function (resolvedUrl) {
                    resolvedUrl = safeImageUrl(resolvedUrl);
                    if (resolvedUrl) {
                        observeImage(image, resolvedUrl);
                    }
                });
            }
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

        main.addEventListener("focus", function () {
            card.classList.add("focused");
            state.returnFocusUid = item.uid || "";
            maybeAppendCards(main);
        });
        main.addEventListener("blur", function () {
            card.classList.remove("focused");
        });

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
