/* SPDX-License-Identifier: GPL-3.0-only */
(function () {
    "use strict";

    var native = window.BlazzingWindowsNative;
    var providers = window.BlazzingProviders;
    var pairing = window.BlazzingPairing;

    var STORAGE_PROFILES = "blazzing.windows.profiles.v2";
    var STORAGE_FAVORITES = "blazzing.windows.favorites.v2";
    var STORAGE_PROGRESS = "blazzing.windows.progress.v2";
    var MAX_RESPONSE_BYTES = 128 * 1024 * 1024;
    var MAX_RENDER = 240;
    var MAX_ARTWORK_BATCH = 24;

    var cardShardCache = {};
    var cardShardPromises = {};
    var cardShardFailureAt = {};
    var artworkQueue = [];
    var artworkTimer = 0;
    var artworkSequence = 0;
    var artworkInFlight = {};
    var artworkSessionCache = {};

    window.BlazzingNet = {
        MAX_RESPONSE_BYTES: MAX_RESPONSE_BYTES,
        text: function (url, options) {
            return native.netText(url, options || {});
        },
        json: function (url, options) {
            return native.netJson(url, options || {});
        },
        postJson: function (url, payload, options) {
            return native.netRequest(
                "POST",
                url,
                JSON.stringify(payload || {}),
                options || {}
            ).then(function (response) {
                var statusCode = Number(response && response.status) || 0;
                if (statusCode < 200 || statusCode >= 300) {
                    throw new Error("Servidor respondeu HTTP " + statusCode + ".");
                }
                try {
                    return JSON.parse(String(response && response.text || "{}"));
                } catch (_error) {
                    throw new Error("O servidor retornou JSON inválido.");
                }
            });
        }
    };

    var state = {
        mode: "xtream",
        profile: null,
        kind: "live",
        catalog: { items: [], categories: [] },
        m3uCatalogs: {},
        xtream: null,
        selectedCategory: "all",
        query: "",
        renderLimit: MAX_RENDER,
        favorites: loadObject(STORAGE_FAVORITES),
        progress: loadObject(STORAGE_PROGRESS),
        currentPlaying: null,
        progressWriteAt: 0,
        seriesParent: null,
        pairingActive: false,
        loadingKind: false,
        fallbackRows: null,
        fallbackCursor: 0,
        fallbackBusy: false
    };

    function $(id) {
        return document.getElementById(id);
    }

    function loadObject(key) {
        try {
            var parsed = JSON.parse(localStorage.getItem(key) || "{}");
            return parsed && typeof parsed === "object" ? parsed : {};
        } catch (_error) {
            return {};
        }
    }

    function saveStateObject(key, value) {
        localStorage.setItem(key, JSON.stringify(value));
    }

    function loadProfiles() {
        try {
            var parsed = JSON.parse(localStorage.getItem(STORAGE_PROFILES) || "[]");
            return Array.isArray(parsed) ? parsed : [];
        } catch (_error) {
            return [];
        }
    }

    function saveProfiles(profiles) {
        localStorage.setItem(STORAGE_PROFILES, JSON.stringify(profiles));
    }

    function profileId() {
        return "p-" + Date.now() + "-" + Math.random().toString(36).slice(2, 9);
    }

    function status(element, message, isError) {
        if (!element) {
            return;
        }
        element.textContent = String(message || "");
        element.classList.toggle("error", !!isError);
    }

    function httpUrl(value) {
        try {
            var url = new URL(String(value || ""));
            return url.protocol === "http:" || url.protocol === "https:";
        } catch (_error) {
            return false;
        }
    }

    function normalizedM3uUrl(value) {
        return String(value || "").replace(/^\s+|\s+$/g, "");
    }

    function refreshSavedProfiles(selectedId) {
        var select = $("saved-profile");
        var profiles = loadProfiles();
        select.innerHTML = '<option value="">Novo perfil</option>';

        profiles.forEach(function (profile) {
            var option = document.createElement("option");
            option.value = profile.id;
            option.textContent = profile.name ||
                (profile.mode === "m3u" ? "M3U / M3U8" : "Xtream Codes");
            select.appendChild(option);
        });

        select.value = selectedId || "";
        $("delete-profile").classList.toggle("hidden", !select.value);
    }

    function setMode(mode) {
        state.mode = mode === "m3u" ? "m3u" : "xtream";
        $("mode-xtream").classList.toggle("active", state.mode === "xtream");
        $("mode-m3u").classList.toggle("active", state.mode === "m3u");
        $("xtream-fields").classList.toggle("hidden", state.mode !== "xtream");
        $("m3u-fields").classList.toggle("hidden", state.mode !== "m3u");
    }

    function fillProfile(profile) {
        profile = profile || {};
        setMode(profile.mode || "xtream");
        $("profile-name").value = profile.name || "";
        $("server").value = profile.server || "";
        $("username").value = profile.username || "";
        $("password").value = profile.password || "";
        $("m3u-url").value = profile.url || "";
    }

    function readProfileForm() {
        var currentId = $("saved-profile").value;
        var name = $("profile-name").value.trim();

        if (state.mode === "m3u") {
            return {
                id: currentId || profileId(),
                mode: "m3u",
                name: name || "Minha lista",
                url: normalizedM3uUrl($("m3u-url").value)
            };
        }

        return {
            id: currentId || profileId(),
            mode: "xtream",
            name: name || "Meu Xtream",
            server: $("server").value.trim(),
            username: $("username").value.trim(),
            password: $("password").value
        };
    }

    function persistProfile(profile) {
        var profiles = loadProfiles();
        var replaced = false;

        profiles = profiles.map(function (item) {
            if (item.id === profile.id) {
                replaced = true;
                return profile;
            }
            return item;
        });

        if (!replaced) {
            profiles.push(profile);
        }

        saveProfiles(profiles);
        refreshSavedProfiles(profile.id);
    }

    function clearCatalogState() {
        state.catalog = { items: [], categories: [] };
        state.selectedCategory = "all";
        state.query = "";
        state.renderLimit = MAX_RENDER;
        state.seriesParent = null;
        $("search").value = "";
        $("back-series").classList.add("hidden");
    }

    function cancelPairing() {
        if (pairing) {
            pairing.stop();
        }
        state.pairingActive = false;
        $("pairing-modal").classList.add("hidden");
        $("pairing-qr").innerHTML = "";
        $("pairing-url").textContent = "";
        $("pairing-retry").classList.add("hidden");
    }

    function showLogin() {
        cancelPairing();
        clearCatalogState();
        $("browser-view").classList.add("hidden");
        $("login-view").classList.remove("hidden");
        $("back-login").classList.add("hidden");
    }

    function showBrowser() {
        $("login-view").classList.add("hidden");
        $("browser-view").classList.remove("hidden");
        $("back-login").classList.remove("hidden");
    }

    function setPairingStatus(message, kind) {
        status($("pairing-status"), message, kind === "error");
        $("pairing-retry").classList.toggle("hidden", kind !== "expired" && kind !== "error");
    }

    function acceptPairedPlaylist(received) {
        var profile = {
            id: profileId(),
            mode: "m3u",
            name: String(received && received.name || "").trim() || "Lista pelo QR",
            url: normalizedM3uUrl(received && received.url)
        };

        state.pairingActive = false;
        $("pairing-modal").classList.add("hidden");
        setMode("m3u");
        $("profile-name").value = profile.name;
        $("m3u-url").value = profile.url;
        $("saved-profile").value = "";
        connectM3u(profile, false).catch(function (error) {
            status($("login-status"), error && error.message ? error.message : String(error), true);
        });
    }

    function startPairing() {
        setMode("m3u");
        if (!pairing) {
            status($("login-status"), "O módulo de QR não foi carregado.", true);
            return;
        }

        cancelPairing();
        state.pairingActive = true;
        $("pairing-modal").classList.remove("hidden");
        $("pairing-retry").classList.add("hidden");
        setPairingStatus("Preparando sessão segura…", "creating");

        pairing.start(acceptPairedPlaylist, setPairingStatus).then(function (session) {
            if (!state.pairingActive) {
                return;
            }
            $("pairing-qr").innerHTML = session.qrSvg;
            $("pairing-url").textContent = session.url;
        }).catch(function (error) {
            if (!state.pairingActive) {
                return;
            }
            setPairingStatus(
                error && error.message ? error.message : "Falha ao criar o QR.",
                "error"
            );
        });
    }

    function itemKey(item) {
        return String(item && item.uid || item && item.url || item && item.name || "");
    }

    function itemName(item) {
        return String(item && item.name || "Sem título");
    }

    function itemArtwork(item) {
        return String(item && item.logo || "");
    }

    function safeImageUrl(value) {
        value = String(value || "").trim();
        if (/^https?:\/\//i.test(value)) {
            return value;
        }
        if (/^\/\//.test(value)) {
            return "https:" + value;
        }
        return "";
    }

    function artworkServiceBase() {
        if (pairing && pairing.baseUrl) {
            return String(pairing.baseUrl() || "").replace(/\/+$/, "");
        }
        return "";
    }

    function artworkKind(item) {
        if (item && (item.kind === "series" || item.kind === "episode")) {
            return "tv";
        }
        if (item && item.kind === "vod") {
            return "movie";
        }
        return "auto";
    }

    function artworkTitle(item) {
        if (item && item.kind === "episode" && item.seriesName) {
            return String(item.seriesName);
        }
        return itemName(item);
    }

    function artworkLookupKey(item) {
        return artworkKind(item) + "|" +
            artworkTitle(item).toLocaleLowerCase("pt-BR").trim();
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

    function scheduleArtworkFlush() {
        if (artworkTimer) {
            return;
        }
        artworkTimer = setTimeout(function () {
            artworkTimer = 0;
            flushArtworkQueue();
        }, 40);
    }

    function flushArtworkQueue() {
        if (!artworkQueue.length) {
            return;
        }

        var base = artworkServiceBase();
        if (!base || !window.BlazzingNet || !window.BlazzingNet.postJson) {
            while (artworkQueue.length) {
                finishArtworkTask(artworkQueue.shift(), "", false);
            }
            return;
        }

        var batch = artworkQueue.splice(0, MAX_ARTWORK_BATCH);
        var payload = {
            items: batch.map(function (task) {
                return {
                    id: task.requestId,
                    title: artworkTitle(task.item),
                    kind: artworkKind(task.item)
                };
            })
        };

        window.BlazzingNet.postJson(
            base + "/api/v1/artwork/resolve",
            payload,
            { timeout: 18000, maxBytes: 256 * 1024 }
        ).then(function (result) {
            var byRequest = {};
            var rows = result && Array.isArray(result.items) ? result.items : [];

            rows.forEach(function (row) {
                byRequest[String(row.id || "")] = row.url || "";
            });

            batch.forEach(function (task) {
                finishArtworkTask(
                    task,
                    byRequest[task.requestId] || "",
                    true
                );
            });
        }).catch(function () {
            batch.forEach(function (task) {
                finishArtworkTask(task, "", false);
            });
        }).then(function () {
            if (artworkQueue.length) {
                scheduleArtworkFlush();
            }
        });
    }

    function resolveOnDemandArtwork(item) {
        if (!item || item.logo) {
            return Promise.resolve(item && item.logo || "");
        }

        if (item.kind !== "vod" && item.kind !== "series" &&
                item.kind !== "episode") {
            return Promise.resolve("");
        }

        var cacheKey = artworkLookupKey(item);
        if (Object.prototype.hasOwnProperty.call(artworkSessionCache, cacheKey)) {
            item.logo = artworkSessionCache[cacheKey] || "";
            return Promise.resolve(item.logo);
        }
        if (artworkInFlight[cacheKey]) {
            return artworkInFlight[cacheKey];
        }

        var requestId = "art-" + (++artworkSequence);
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

    function resolveCardLogo(item) {
        if (!item) {
            return Promise.resolve("");
        }
        if (safeImageUrl(item.logo)) {
            return Promise.resolve(item.logo);
        }

        var base = String(item.cardIndexBase || "").replace(/\/+$/, "");
        var key = String(item.cardKey || "");
        var version = String(item.cardIndexVersion || "");
        if (!base || !key || !httpUrl(base)) {
            return resolveOnDemandArtwork(item);
        }

        var prefixLength = parseInt(item.cardIndexShardLength || 1, 10);
        if (!(prefixLength >= 1 && prefixLength <= 4)) {
            prefixLength = 1;
        }

        var prefix = key.slice(0, prefixLength);
        var cacheKey = base + "|" + version + "|" + prefix;

        if (cardShardCache[cacheKey]) {
            item.logo = safeImageUrl(cardShardCache[cacheKey][key] || "");
            return item.logo ?
                Promise.resolve(item.logo) :
                resolveOnDemandArtwork(item);
        }

        var failedAt = Number(cardShardFailureAt[cacheKey] || 0);
        if (failedAt && Date.now() - failedAt < 30000) {
            return resolveOnDemandArtwork(item);
        }

        if (!cardShardPromises[cacheKey]) {
            var url = base + "/" + prefix + ".json";
            if (version) {
                url += "?v=" + encodeURIComponent(version);
            }

            cardShardPromises[cacheKey] = native.netJson(url, {
                timeout: 20000,
                maxBytes: 4 * 1024 * 1024
            }).then(function (rows) {
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
            item.logo = safeImageUrl(rows[key] || "");
            return item.logo ?
                item.logo :
                resolveOnDemandArtwork(item);
        });
    }

    function loadCardImage(image, fallback, item, url, allowFallback) {
        url = safeImageUrl(url);
        if (!url || !image || !fallback) {
            return;
        }

        image.alt = itemName(item);
        image.referrerPolicy = "no-referrer";
        image.onload = function () {
            fallback.style.display = "none";
        };
        image.onerror = function () {
            image.removeAttribute("src");
            fallback.style.display = "grid";

            if (!allowFallback || item._artworkFallbackTried) {
                return;
            }

            item._artworkFallbackTried = true;
            item.logo = "";
            resolveCardLogo(item).then(function (resolved) {
                if (safeImageUrl(resolved)) {
                    loadCardImage(image, fallback, item, resolved, false);
                }
            }).catch(function () {});
        };
        image.src = url;
    }

    function itemCategory(item) {
        return String(item && item.categoryId || "");
    }

    function categoryName(category) {
        return String(category && (category.name || category.category_name) || "Outros");
    }

    function categoryId(category) {
        return String(category && (category.id || category.category_id) || "");
    }

    function sectionTitle(kind) {
        if (kind === "vod") {
            return "Filmes";
        }
        if (kind === "series") {
            return "Séries";
        }
        return "TV";
    }

    async function ensureM3uCached(profile, forceReload) {
        var cached = null;

        if (!forceReload) {
            cached = await native.cachedPlaylist(profile.url);
        }

        if (cached && cached.text) {
            return cached.text;
        }

        status($("login-status"), "Baixando playlist…", false);
        var text = await native.netText(profile.url, {
            timeout: 60000,
            maxBytes: MAX_RESPONSE_BYTES
        });

        if (text.indexOf("#EXTM3U") === -1 && text.indexOf("#EXTINF:") === -1) {
            throw new Error("O conteúdo recebido não parece ser uma playlist M3U.");
        }

        await native.saveCachedPlaylist(profile.url, text);
        return text;
    }

    async function loadStoredM3uCatalog(profile, kind, options) {
        var cached = await native.cachedPlaylist(profile.url);
        if (!cached || !cached.text) {
            throw new Error("A playlist local não está disponível.");
        }

        options = options || {
            onlyKind: kind,
            seriesSummaryOnly: kind === "series"
        };

        var result = await providers.parseM3uAsync(cached.text, profile.url, options);
        return result[kind] || { items: [], categories: [] };
    }

    async function connectM3u(profile, forceReload) {
        if (!httpUrl(profile.url)) {
            throw new Error("Informe uma URL M3U/M3U8 HTTP ou HTTPS.");
        }

        await ensureM3uCached(profile, !!forceReload || $("m3u-refresh").checked);
        state.profile = profile;
        state.xtream = null;
        state.m3uCatalogs = {};
        persistProfile(profile);
        showBrowser();
        await loadKind("live");
        status($("login-status"), "", false);
    }

    async function connectXtream(profile) {
        if (!profile.server || !profile.username || !profile.password) {
            throw new Error("Preencha servidor, usuário e senha.");
        }

        status($("login-status"), "Validando credenciais…", false);
        var client = new providers.XtreamClient(profile);
        await client.authenticate();

        state.profile = profile;
        state.xtream = client;
        state.m3uCatalogs = {};
        persistProfile(profile);
        showBrowser();
        await loadKind("live");
        status($("login-status"), "", false);
    }

    function configureContentTabs() {
        Array.prototype.forEach.call(document.querySelectorAll(".content-tab"), function (button) {
            var kind = button.getAttribute("data-kind");
            button.disabled = state.loadingKind;
            button.classList.toggle("active", kind === state.kind);
        });
    }

    async function loadKind(kind) {
        if (state.loadingKind || !state.profile) {
            return;
        }
        if (kind !== "live" && kind !== "vod" && kind !== "series") {
            return;
        }

        state.loadingKind = true;
        state.kind = kind;
        state.seriesParent = null;
        state.selectedCategory = "all";
        state.query = "";
        state.renderLimit = MAX_RENDER;
        state.catalog = { items: [], categories: [] };
        $("search").value = "";
        $("back-series").classList.add("hidden");
        status($("catalog-status"), "Carregando " + sectionTitle(kind).toLowerCase() + "…", false);
        configureContentTabs();

        try {
            var catalog;
            if (state.profile.mode === "m3u") {
                catalog = state.m3uCatalogs[kind];
                if (!catalog) {
                    catalog = await loadStoredM3uCatalog(state.profile, kind);
                    state.m3uCatalogs[kind] = catalog;
                }
            } else {
                catalog = await state.xtream.load(kind);
            }

            state.catalog = catalog || { items: [], categories: [] };
            status($("catalog-status"), "", false);
        } finally {
            state.loadingKind = false;
            configureContentTabs();
            renderAll();
        }
    }

    function filteredItems() {
        var q = state.query.trim().toLocaleLowerCase("pt-BR");

        return (state.catalog.items || []).filter(function (item) {
            if (state.selectedCategory === "__favorites" &&
                    !state.favorites[itemKey(item)]) {
                return false;
            }

            if (state.selectedCategory !== "all" &&
                    state.selectedCategory !== "__favorites" &&
                    itemCategory(item) !== state.selectedCategory) {
                return false;
            }

            if (q && itemName(item).toLocaleLowerCase("pt-BR").indexOf(q) < 0) {
                return false;
            }
            return true;
        });
    }

    function renderCategories() {
        var root = $("categories");
        var counts = {};
        root.innerHTML = "";

        (state.catalog.items || []).forEach(function (item) {
            var id = itemCategory(item);
            counts[id] = (counts[id] || 0) + 1;
        });

        var favoritesButton = document.createElement("button");
        favoritesButton.className = "category" +
            (state.selectedCategory === "__favorites" ? " active" : "");
        favoritesButton.textContent = "★ Favoritos";
        favoritesButton.addEventListener("click", function () {
            state.selectedCategory = "__favorites";
            state.renderLimit = MAX_RENDER;
            renderAll();
        });
        root.appendChild(favoritesButton);

        (state.catalog.categories || []).forEach(function (category) {
            var id = categoryId(category);
            var button = document.createElement("button");
            button.className = "category" + (state.selectedCategory === id ? " active" : "");
            button.textContent = categoryName(category) +
                (counts[id] ? " (" + counts[id] + ")" : "");
            button.addEventListener("click", function () {
                state.selectedCategory = id;
                state.renderLimit = MAX_RENDER;
                renderAll();
            });
            root.appendChild(button);
        });

        $("all-category").classList.toggle("active", state.selectedCategory === "all");
    }

    function renderCards() {
        var root = $("cards");
        var all = filteredItems();
        var visible = all.slice(0, state.renderLimit);
        root.innerHTML = "";

        visible.forEach(function (item) {
            var fragment = $("card-template").content.cloneNode(true);
            var card = fragment.querySelector(".media-card");
            var image = fragment.querySelector(".poster");
            var fallback = fragment.querySelector(".poster-fallback");
            var title = fragment.querySelector(".media-title");
            var meta = fragment.querySelector(".media-meta");
            var favorite = fragment.querySelector(".favorite");
            var art = itemArtwork(item);
            var key = itemKey(item);

            title.textContent = itemName(item);

            if (item.kind === "series" && item.episodeCount) {
                meta.textContent = item.categoryName + " · " + item.episodeCount + " episódios";
            } else if (item.kind === "episode") {
                meta.textContent = "T" + item.season + " · E" + item.episode;
            } else {
                meta.textContent = item.categoryName || sectionTitle(state.kind);
            }

            if (safeImageUrl(art)) {
                loadCardImage(image, fallback, item, art, true);
            } else {
                resolveCardLogo(item).then(function (resolved) {
                    if (safeImageUrl(resolved)) {
                        loadCardImage(image, fallback, item, resolved, false);
                    }
                }).catch(function () {});
            }

            if (item.kind === "episode") {
                card.classList.add("episode-card");
            }

            favorite.classList.toggle("active", !!state.favorites[key]);
            favorite.textContent = state.favorites[key] ? "★" : "☆";
            favorite.addEventListener("click", function (event) {
                event.stopPropagation();
                toggleFavorite(item);
            });

            card.addEventListener("click", function () {
                activateItem(item);
            });
            card.addEventListener("keydown", function (event) {
                if (event.key === "Enter") {
                    event.preventDefault();
                    activateItem(item);
                }
            });

            root.appendChild(fragment);
        });

        if (all.length > visible.length) {
            var more = document.createElement("button");
            more.className = "ghost more-button";
            more.textContent = "Mostrar mais (" + (all.length - visible.length) + ")";
            more.addEventListener("click", function () {
                state.renderLimit += MAX_RENDER;
                renderCards();
            });
            root.appendChild(more);
        }

        $("catalog-title").textContent = state.seriesParent ?
            state.seriesParent.title : sectionTitle(state.kind);
        $("catalog-count").textContent = all.length + (all.length === 1 ? " item" : " itens");
    }

    function renderAll() {
        configureContentTabs();
        renderCategories();
        renderCards();
    }

    function toggleFavorite(item) {
        var key = itemKey(item);
        if (state.favorites[key]) {
            delete state.favorites[key];
        } else {
            state.favorites[key] = true;
        }
        saveStateObject(STORAGE_FAVORITES, state.favorites);
        renderCards();
    }

    function showSeries(details, series) {
        var seasons = details.seasons || [];
        var episodes = details.episodes || [];

        state.seriesParent = {
            title: details.title || itemName(series),
            catalog: state.catalog,
            selectedCategory: state.selectedCategory
        };

        state.catalog = {
            categories: seasons.map(function (season) {
                return { id: "season:" + season, name: "Temporada " + season };
            }),
            items: episodes.map(function (episode) {
                episode.categoryId = "season:" + episode.season;
                episode.categoryName = "Temporada " + episode.season;
                return episode;
            })
        };
        state.selectedCategory = "all";
        state.query = "";
        state.renderLimit = MAX_RENDER;
        $("search").value = "";
        $("back-series").classList.remove("hidden");
        status($("catalog-status"), "", false);
        renderAll();
    }

    async function openSeries(series) {
        status($("catalog-status"), "Carregando episódios…", false);

        if (series.source === "m3u") {
            var catalog = await loadStoredM3uCatalog(state.profile, "series", {
                onlyKind: "series",
                seriesFilterKey: series.seriesKey || series.name
            });
            var fullSeries = catalog.items && catalog.items[0];
            if (!fullSeries || !Array.isArray(fullSeries.episodes) || !fullSeries.episodes.length) {
                throw new Error("Nenhum episódio foi encontrado para esta série.");
            }

            var seasons = [];
            fullSeries.episodes.forEach(function (episode) {
                if (seasons.indexOf(episode.season) === -1) {
                    seasons.push(episode.season);
                }
            });
            seasons.sort(function (a, b) { return a - b; });

            showSeries({
                title: series.name,
                seasons: seasons,
                episodes: fullSeries.episodes
            }, series);
            return;
        }

        var details = await state.xtream.seriesInfo(series);
        showSeries(details, series);
    }

    function closeSeries() {
        if (!state.seriesParent) {
            return;
        }

        state.catalog = state.seriesParent.catalog;
        state.selectedCategory = state.seriesParent.selectedCategory || "all";
        state.seriesParent = null;
        state.query = "";
        state.renderLimit = MAX_RENDER;
        $("search").value = "";
        $("back-series").classList.add("hidden");
        renderAll();
    }

    function fallbackShardUrl(item) {
        var id = String(item && item.fallbackId || "");
        var base = String(item && item.fallbackIndexBase || "").replace(/\/+$/, "");
        var shardLength = parseInt(item && item.fallbackIndexShardLength, 10) || 2;
        var version = String(item && item.fallbackIndexVersion || "");

        if (!id || !base || !httpUrl(base)) {
            return "";
        }

        shardLength = Math.max(1, Math.min(4, shardLength));
        return base + "/" + id.slice(0, shardLength) + ".json" +
            (version ? "?v=" + encodeURIComponent(version) : "");
    }

    async function loadFallbackRows(item) {
        if (state.fallbackRows) {
            return state.fallbackRows;
        }

        var shardUrl = fallbackShardUrl(item);
        var id = String(item && item.fallbackId || "");
        if (!shardUrl || !id) {
            state.fallbackRows = [];
            return state.fallbackRows;
        }

        try {
            var payload = await native.netJson(shardUrl, {
                timeout: 10000,
                maxBytes: 4 * 1024 * 1024
            });
            var rows = payload && Array.isArray(payload[id]) ? payload[id] : [];
            var seen = {};
            state.fallbackRows = [];

            rows.forEach(function (row) {
                if (!Array.isArray(row) || !row.length) {
                    return;
                }
                var url = String(row[0] || "");
                if (!httpUrl(url) || url === item.url || seen[url]) {
                    return;
                }
                seen[url] = true;
                state.fallbackRows.push({
                    url: url,
                    referer: String(row[2] || ""),
                    userAgent: String(row[3] || "")
                });
            });
        } catch (_error) {
            state.fallbackRows = [];
        }

        return state.fallbackRows;
    }

    async function openPlayerSource(item, source, resumeMs) {
        return native.playerOpen({
            url: source.url,
            referer: source.referer || "",
            userAgent: source.userAgent || "",
            resumeMs: resumeMs,
            kind: item.kind
        });
    }

    async function tryFallback(reason) {
        if (!state.currentPlaying || state.fallbackBusy) {
            return;
        }

        state.fallbackBusy = true;
        try {
            var rows = await loadFallbackRows(state.currentPlaying.item);
            if (state.fallbackCursor >= rows.length) {
                status(
                    $("catalog-status"),
                    reason || "Nenhuma fonte alternativa pôde ser reproduzida.",
                    true
                );
                return;
            }

            var source = rows[state.fallbackCursor++];
            status(
                $("catalog-status"),
                "Fonte indisponível; tentando alternativa " + state.fallbackCursor + "…",
                false
            );
            await openPlayerSource(
                state.currentPlaying.item,
                source,
                Number(state.progress[state.currentPlaying.key] || 0)
            );
        } finally {
            state.fallbackBusy = false;
        }
    }

    async function activateItem(item) {
        try {
            if (item.kind === "series") {
                await openSeries(item);
                return;
            }

            if (!httpUrl(item.url)) {
                throw new Error("O item não possui uma URL de reprodução válida.");
            }

            var key = itemKey(item);
            var resumeMs = item.kind === "live" ? 0 : Number(state.progress[key] || 0);

            state.currentPlaying = {
                item: item,
                key: key,
                kind: item.kind
            };
            state.fallbackRows = null;
            state.fallbackCursor = 0;
            state.fallbackBusy = false;

            status($("catalog-status"), "Abrindo " + itemName(item) + "…", false);
            await openPlayerSource(item, {
                url: item.url,
                referer: item.referer || "",
                userAgent: item.userAgent || ""
            }, resumeMs);
        } catch (error) {
            var message = error && error.message ? error.message : String(error);
            await tryFallback(message);
        }
    }

    async function onConnect() {
        var button = $("connect");
        button.disabled = true;

        try {
            var profile = readProfileForm();
            if (profile.mode === "m3u") {
                await connectM3u(profile, false);
            } else {
                await connectXtream(profile);
            }
        } catch (error) {
            status(
                $("login-status"),
                error && error.message ? error.message : String(error),
                true
            );
        } finally {
            button.disabled = false;
        }
    }

    function deleteSelectedProfile() {
        var id = $("saved-profile").value;
        if (!id) {
            return;
        }

        var profiles = loadProfiles().filter(function (profile) {
            return profile.id !== id;
        });
        saveProfiles(profiles);
        refreshSavedProfiles("");
        fillProfile(null);
    }

    function installEvents() {
        $("mode-xtream").addEventListener("click", function () {
            setMode("xtream");
        });
        $("mode-m3u").addEventListener("click", function () {
            setMode("m3u");
        });
        $("connect").addEventListener("click", onConnect);
        $("pair-button").addEventListener("click", startPairing);
        $("pairing-retry").addEventListener("click", startPairing);
        $("pairing-cancel").addEventListener("click", cancelPairing);
        $("delete-profile").addEventListener("click", deleteSelectedProfile);
        $("back-login").addEventListener("click", showLogin);
        $("back-series").addEventListener("click", closeSeries);
        $("fullscreen").addEventListener("click", function () {
            if (native && native.toggleFullscreen) {
                native.toggleFullscreen();
            }
        });

        $("saved-profile").addEventListener("change", function () {
            var id = this.value;
            var profile = loadProfiles().find(function (item) {
                return item.id === id;
            });
            fillProfile(profile || null);
            $("delete-profile").classList.toggle("hidden", !id);
        });

        $("all-category").addEventListener("click", function () {
            state.selectedCategory = "all";
            state.renderLimit = MAX_RENDER;
            renderAll();
        });

        $("search").addEventListener("input", function () {
            state.query = this.value;
            state.renderLimit = MAX_RENDER;
            renderCards();
        });

        Array.prototype.forEach.call(document.querySelectorAll(".content-tab"), function (button) {
            button.addEventListener("click", function () {
                var kind = button.getAttribute("data-kind");
                if (kind === state.kind && !state.seriesParent) {
                    return;
                }
                loadKind(kind).catch(function (error) {
                    status(
                        $("catalog-status"),
                        error && error.message ? error.message : String(error),
                        true
                    );
                });
            });
        });

        document.addEventListener("keydown", function (event) {
            if (state.pairingActive && event.key === "Escape") {
                cancelPairing();
                return;
            }

            if (event.ctrlKey && event.key === "1") {
                document.querySelector('[data-kind="live"]').click();
            } else if (event.ctrlKey && event.key === "2") {
                document.querySelector('[data-kind="vod"]').click();
            } else if (event.ctrlKey && event.key === "3") {
                document.querySelector('[data-kind="series"]').click();
            } else if (event.ctrlKey && event.key.toLowerCase() === "f" &&
                    !$("browser-view").classList.contains("hidden")) {
                event.preventDefault();
                $("search").focus();
            } else if (event.key === "Escape" && state.seriesParent) {
                closeSeries();
            }
        });

        native.onPlayerState(function (payload) {
            if (!payload) {
                return;
            }

            status($("catalog-status"), payload.text || "", !!payload.error);
            if (payload.error && state.currentPlaying) {
                tryFallback(payload.text || "Falha de reprodução.").catch(function () {});
            }
        });

        native.onPlayerTime(function (payload) {
            if (!payload || !state.currentPlaying ||
                    state.currentPlaying.kind === "live") {
                return;
            }

            var now = Date.now();
            if (now - state.progressWriteAt < 5000) {
                return;
            }

            state.progressWriteAt = now;
            state.progress[state.currentPlaying.key] =
                Math.max(0, Number(payload.milliseconds) || 0);
            saveStateObject(STORAGE_PROGRESS, state.progress);
        });
    }

    function bootstrap() {
        refreshSavedProfiles("");
        setMode("xtream");

        if (!native || !providers) {
            status(
                $("login-status"),
                "Os módulos nativos do Windows não foram carregados corretamente.",
                true
            );
            $("connect").disabled = true;
            return;
        }

        installEvents();
    }

    bootstrap();
}());
