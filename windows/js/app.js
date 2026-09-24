/* SPDX-License-Identifier: GPL-3.0-only */
(function () {
    "use strict";

    var native = window.BlazzingWindowsNative;
    var STORAGE_PROFILES = "blazzing.windows.profiles.v2";
    var STORAGE_FAVORITES = "blazzing.windows.favorites.v2";
    var STORAGE_PROGRESS = "blazzing.windows.progress.v2";
    var MAX_RENDER = 240;

    var state = {
        mode: "xtream",
        profile: null,
        kind: "live",
        categories: [],
        items: [],
        selectedCategory: "",
        query: "",
        renderLimit: MAX_RENDER,
        favorites: loadObject(STORAGE_FAVORITES),
        progress: loadObject(STORAGE_PROGRESS),
        currentPlaying: null,
        progressWriteAt: 0,
        seriesParent: null
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

    function saveStateObject(key, value) {
        localStorage.setItem(key, JSON.stringify(value));
    }

    function status(element, message, isError) {
        element.textContent = String(message || "");
        element.classList.toggle("error", !!isError);
    }

    function normalizeServer(value) {
        return String(value || "").trim().replace(/\/+$/, "");
    }

    function httpUrl(value) {
        try {
            var url = new URL(String(value || ""));
            return url.protocol === "http:" || url.protocol === "https:";
        } catch (_error) {
            return false;
        }
    }

    function encode(value) {
        return encodeURIComponent(String(value == null ? "" : value));
    }

    function profileId() {
        return "p-" + Date.now() + "-" + Math.random().toString(36).slice(2, 9);
    }

    function refreshSavedProfiles(selectedId) {
        var select = $("saved-profile");
        var profiles = loadProfiles();
        select.innerHTML = '<option value="">Novo perfil</option>';
        profiles.forEach(function (profile) {
            var option = document.createElement("option");
            option.value = profile.id;
            option.textContent = profile.name || (profile.mode === "m3u" ? "M3U" : "Xtream");
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
                url: $("m3u-url").value.trim()
            };
        }
        return {
            id: currentId || profileId(),
            mode: "xtream",
            name: name || "Meu Xtream",
            server: normalizeServer($("server").value),
            username: $("username").value.trim(),
            password: $("password").value
        };
    }

    function persistProfile(profile) {
        var profiles = loadProfiles();
        var found = false;
        profiles = profiles.map(function (item) {
            if (item.id === profile.id) {
                found = true;
                return profile;
            }
            return item;
        });
        if (!found) {
            profiles.push(profile);
        }
        saveProfiles(profiles);
        refreshSavedProfiles(profile.id);
    }

    function showLogin() {
        state.seriesParent = null;
        $("browser-view").classList.add("hidden");
        $("login-view").classList.remove("hidden");
        $("back-login").classList.add("hidden");
        $("back-series").classList.add("hidden");
    }

    function showBrowser() {
        $("login-view").classList.add("hidden");
        $("browser-view").classList.remove("hidden");
        $("back-login").classList.remove("hidden");
    }

    function xtreamApi(profile, action, extra) {
        var url = profile.server + "/player_api.php?username=" + encode(profile.username) +
            "&password=" + encode(profile.password);
        if (action) {
            url += "&action=" + encode(action);
        }
        Object.keys(extra || {}).forEach(function (key) {
            url += "&" + encode(key) + "=" + encode(extra[key]);
        });
        return url;
    }

    async function validateXtream(profile) {
        var payload = await native.netJson(xtreamApi(profile, "", null), {
            timeout: 20000,
            maxBytes: 2 * 1024 * 1024
        });
        var info = payload && payload.user_info;
        if (!info || String(info.auth) !== "1") {
            throw new Error("Credenciais Xtream rejeitadas pelo servidor.");
        }
    }

    function categoryName(category) {
        return String(category.category_name || category.name || "Sem categoria");
    }

    function categoryId(category) {
        return String(category.category_id == null ? category.id || "" : category.category_id);
    }

    function itemCategory(item) {
        return String(item.category_id == null ? item.group || "" : item.category_id);
    }

    function itemName(item) {
        return String(item.name || item.title || "Sem título");
    }

    function itemArtwork(item) {
        return String(
            item.stream_icon || item.cover || item.movie_image || item.logo || item.tvgLogo || ""
        );
    }

    function itemKey(item) {
        if (item._m3uUrl) {
            return "m3u:" + item._m3uUrl;
        }
        if (item._episodeId) {
            return "episode:" + item._episodeId;
        }
        if (item.series_id != null) {
            return "series:" + item.series_id;
        }
        if (item.stream_id != null) {
            return state.kind + ":" + item.stream_id;
        }
        return state.kind + ":" + itemName(item);
    }

    function playbackUrl(item) {
        if (item._m3uUrl) {
            return item._m3uUrl;
        }
        if (item._episodeUrl) {
            return item._episodeUrl;
        }
        if (item.direct_source && httpUrl(item.direct_source)) {
            return item.direct_source;
        }
        if (!state.profile || state.profile.mode !== "xtream") {
            return "";
        }

        var base = state.profile.server;
        var user = encode(state.profile.username);
        var pass = encode(state.profile.password);
        var id = encode(item.stream_id);
        if (state.kind === "live") {
            return base + "/live/" + user + "/" + pass + "/" + id + ".ts";
        }
        if (state.kind === "vod") {
            return base + "/movie/" + user + "/" + pass + "/" + id + "." +
                encode(item.container_extension || "mp4");
        }
        return "";
    }

    function parseM3u(text) {
        var lines = String(text || "").replace(/\r/g, "").split("\n");
        var items = [];
        var pending = null;

        lines.forEach(function (raw) {
            var line = raw.trim();
            if (!line) {
                return;
            }
            if (line.indexOf("#EXTINF:") === 0) {
                var comma = line.lastIndexOf(",");
                var attrsPart = comma >= 0 ? line.slice(0, comma) : line;
                var title = comma >= 0 ? line.slice(comma + 1).trim() : "Canal";
                var attrs = {};
                var re = /([A-Za-z0-9_-]+)="([^"]*)"/g;
                var match;
                while ((match = re.exec(attrsPart))) {
                    attrs[match[1]] = match[2];
                }
                pending = {
                    name: title || attrs["tvg-name"] || "Canal",
                    group: attrs["group-title"] || "Sem categoria",
                    tvgLogo: attrs["tvg-logo"] || "",
                    tvgId: attrs["tvg-id"] || ""
                };
                return;
            }
            if (line[0] === "#") {
                return;
            }
            if (pending && httpUrl(line)) {
                pending._m3uUrl = line;
                items.push(pending);
                pending = null;
            }
        });

        return items;
    }

    function categoriesFromM3u(items) {
        var seen = {};
        var result = [];
        items.forEach(function (item) {
            var group = String(item.group || "Sem categoria");
            if (!seen[group]) {
                seen[group] = true;
                result.push({ category_id: group, category_name: group });
            }
        });
        return result;
    }

    async function connectM3u(profile) {
        if (!httpUrl(profile.url)) {
            throw new Error("Informe uma URL M3U/M3U8 HTTP ou HTTPS.");
        }

        var cached = null;
        if (!$("m3u-refresh").checked) {
            cached = await native.cachedPlaylist(profile.url);
        }

        var text;
        if (cached && cached.text) {
            text = cached.text;
            status($("login-status"), "Abrindo playlist salva localmente…", false);
        } else {
            status($("login-status"), "Baixando playlist…", false);
            text = await native.netText(profile.url, {
                timeout: 60000,
                maxBytes: 128 * 1024 * 1024
            });
            await native.saveCachedPlaylist(profile.url, text);
        }

        var items = parseM3u(text);
        if (!items.length) {
            throw new Error("A playlist não contém entradas HTTP/HTTPS reconhecidas.");
        }

        state.profile = profile;
        state.kind = "live";
        state.items = items;
        state.categories = categoriesFromM3u(items);
        state.selectedCategory = "";
        state.query = "";
        state.renderLimit = MAX_RENDER;
        state.seriesParent = null;
        persistProfile(profile);
        configureContentTabs();
        showBrowser();
        renderAll();
    }

    async function loadXtreamKind(kind) {
        state.kind = kind;
        state.selectedCategory = "";
        state.query = "";
        state.renderLimit = MAX_RENDER;
        state.seriesParent = null;
        $("search").value = "";
        $("back-series").classList.add("hidden");
        status($("catalog-status"), "Carregando catálogo…", false);

        var actions = {
            live: ["get_live_categories", "get_live_streams"],
            vod: ["get_vod_categories", "get_vod_streams"],
            series: ["get_series_categories", "get_series"]
        };
        var pair = actions[kind];
        var responses = await Promise.all([
            native.netJson(xtreamApi(state.profile, pair[0], null), {
                timeout: 30000,
                maxBytes: 16 * 1024 * 1024
            }),
            native.netJson(xtreamApi(state.profile, pair[1], null), {
                timeout: 60000,
                maxBytes: 128 * 1024 * 1024
            })
        ]);

        state.categories = Array.isArray(responses[0]) ? responses[0] : [];
        state.items = Array.isArray(responses[1]) ? responses[1] : [];
        status($("catalog-status"), "", false);
        renderAll();
    }

    async function connectXtream(profile) {
        if (!httpUrl(profile.server) || !profile.username || !profile.password) {
            throw new Error("Preencha servidor, usuário e senha.");
        }
        status($("login-status"), "Validando credenciais…", false);
        await validateXtream(profile);
        state.profile = profile;
        persistProfile(profile);
        configureContentTabs();
        showBrowser();
        await loadXtreamKind("live");
    }

    function configureContentTabs() {
        Array.prototype.forEach.call(document.querySelectorAll(".content-tab"), function (button) {
            var disabled = state.profile && state.profile.mode === "m3u" &&
                button.getAttribute("data-kind") !== "live";
            button.disabled = disabled;
            button.classList.toggle("active", button.getAttribute("data-kind") === state.kind);
        });
    }

    function filteredItems() {
        var q = state.query.trim().toLocaleLowerCase("pt-BR");
        return state.items.filter(function (item) {
            if (state.selectedCategory === "__favorites" && !state.favorites[itemKey(item)]) {
                return false;
            }
            if (state.selectedCategory && state.selectedCategory !== "__favorites" &&
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
        root.innerHTML = "";

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

        state.categories.forEach(function (category) {
            var id = categoryId(category);
            var button = document.createElement("button");
            button.className = "category" + (state.selectedCategory === id ? " active" : "");
            button.textContent = categoryName(category);
            button.addEventListener("click", function () {
                state.selectedCategory = id;
                state.renderLimit = MAX_RENDER;
                renderAll();
            });
            root.appendChild(button);
        });

        $("all-category").classList.toggle("active", !state.selectedCategory);
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
            meta.textContent = item._seasonLabel || item.group ||
                (state.kind === "series" ? "Série" : state.kind === "vod" ? "Filme" : "TV");
            if (art && httpUrl(art)) {
                image.src = art;
                image.alt = itemName(item);
                image.addEventListener("load", function () {
                    fallback.style.display = "none";
                });
                image.addEventListener("error", function () {
                    image.removeAttribute("src");
                    fallback.style.display = "grid";
                });
            }
            if (item._episodeId) {
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
            more.className = "ghost";
            more.textContent = "Mostrar mais (" + (all.length - visible.length) + ")";
            more.addEventListener("click", function () {
                state.renderLimit += MAX_RENDER;
                renderCards();
            });
            root.appendChild(more);
        }

        var titleMap = { live: "TV", vod: "Filmes", series: "Séries" };
        $("catalog-title").textContent = state.seriesParent ?
            state.seriesParent.name : titleMap[state.kind];
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

    async function openSeries(item) {
        status($("catalog-status"), "Carregando episódios…", false);
        var payload = await native.netJson(
            xtreamApi(state.profile, "get_series_info", { series_id: item.series_id }),
            { timeout: 30000, maxBytes: 32 * 1024 * 1024 }
        );
        var episodes = [];
        var groups = payload && payload.episodes && typeof payload.episodes === "object" ?
            payload.episodes : {};

        Object.keys(groups).sort(function (a, b) {
            return Number(a) - Number(b);
        }).forEach(function (season) {
            var list = Array.isArray(groups[season]) ? groups[season] : [];
            list.forEach(function (episode) {
                var info = episode.info || {};
                var extension = episode.container_extension || "mp4";
                episodes.push({
                    name: episode.title || ("Episódio " + (episode.episode_num || "")),
                    category_id: String(season),
                    _seasonLabel: "Temporada " + season,
                    _episodeId: episode.id || (item.series_id + "-" + season + "-" + episode.episode_num),
                    _episodeUrl: state.profile.server + "/series/" +
                        encode(state.profile.username) + "/" + encode(state.profile.password) + "/" +
                        encode(episode.id) + "." + encode(extension),
                    cover: info.movie_image || info.cover_big || item.cover || "",
                    plot: info.plot || ""
                });
            });
        });

        if (!episodes.length) {
            throw new Error("O servidor não retornou episódios para esta série.");
        }

        state.seriesParent = {
            name: itemName(item),
            items: state.items,
            categories: state.categories,
            selectedCategory: state.selectedCategory
        };
        state.items = episodes;
        state.categories = Object.keys(groups).sort(function (a, b) {
            return Number(a) - Number(b);
        }).map(function (season) {
            return { category_id: String(season), category_name: "Temporada " + season };
        });
        state.selectedCategory = "";
        state.query = "";
        state.renderLimit = MAX_RENDER;
        $("search").value = "";
        $("back-series").classList.remove("hidden");
        status($("catalog-status"), "", false);
        renderAll();
    }

    function closeSeries() {
        if (!state.seriesParent) {
            return;
        }
        state.items = state.seriesParent.items;
        state.categories = state.seriesParent.categories;
        state.selectedCategory = state.seriesParent.selectedCategory || "";
        state.seriesParent = null;
        state.query = "";
        state.renderLimit = MAX_RENDER;
        $("search").value = "";
        $("back-series").classList.add("hidden");
        renderAll();
    }

    async function activateItem(item) {
        try {
            if (state.kind === "series" && !item._episodeUrl) {
                await openSeries(item);
                return;
            }

            var url = playbackUrl(item);
            if (!httpUrl(url)) {
                throw new Error("O item não possui uma URL de reprodução válida.");
            }
            var key = itemKey(item);
            var resumeMs = state.kind === "live" ? 0 : Number(state.progress[key] || 0);
            state.currentPlaying = { item: item, key: key, kind: state.kind };
            status($("catalog-status"), "Abrindo " + itemName(item) + "…", false);
            await native.playerOpen({
                url: url,
                resumeMs: resumeMs,
                kind: state.kind
            });
        } catch (error) {
            status($("catalog-status"), error && error.message ? error.message : String(error), true);
        }
    }

    async function onConnect() {
        var button = $("connect");
        button.disabled = true;
        try {
            var profile = readProfileForm();
            if (profile.mode === "m3u") {
                await connectM3u(profile);
            } else {
                await connectXtream(profile);
            }
            status($("login-status"), "", false);
        } catch (error) {
            status($("login-status"), error && error.message ? error.message : String(error), true);
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
        $("mode-xtream").addEventListener("click", function () { setMode("xtream"); });
        $("mode-m3u").addEventListener("click", function () { setMode("m3u"); });
        $("connect").addEventListener("click", onConnect);
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
            var profile = loadProfiles().find(function (item) { return item.id === id; });
            fillProfile(profile || null);
            $("delete-profile").classList.toggle("hidden", !id);
        });

        $("all-category").addEventListener("click", function () {
            state.selectedCategory = "";
            state.renderLimit = MAX_RENDER;
            renderAll();
        });

        $("search").addEventListener("input", function () {
            state.query = this.value;
            state.renderLimit = MAX_RENDER;
            renderCards();
        });

        Array.prototype.forEach.call(document.querySelectorAll(".content-tab"), function (button) {
            button.addEventListener("click", async function () {
                var kind = button.getAttribute("data-kind");
                if (!state.profile || state.profile.mode !== "xtream" || kind === state.kind) {
                    return;
                }
                try {
                    await loadXtreamKind(kind);
                } catch (error) {
                    status($("catalog-status"), error && error.message ? error.message : String(error), true);
                }
            });
        });

        document.addEventListener("keydown", function (event) {
            if (event.ctrlKey && event.key === "1") {
                document.querySelector('[data-kind="live"]').click();
            } else if (event.ctrlKey && event.key === "2") {
                document.querySelector('[data-kind="vod"]').click();
            } else if (event.ctrlKey && event.key === "3") {
                document.querySelector('[data-kind="series"]').click();
            } else if (event.ctrlKey && event.key.toLowerCase() === "f" && !$("browser-view").classList.contains("hidden")) {
                event.preventDefault();
                $("search").focus();
            } else if (event.key === "Escape" && state.seriesParent) {
                closeSeries();
            }
        });

        if (native) {
            native.onPlayerState(function (payload) {
                if (!payload) {
                    return;
                }
                status($("catalog-status"), payload.text || "", !!payload.error);
                if (payload.error) {
                    state.currentPlaying = null;
                }
            });
            native.onPlayerTime(function (payload) {
                if (!payload || !state.currentPlaying || state.currentPlaying.kind === "live") {
                    return;
                }
                var now = Date.now();
                if (now - state.progressWriteAt < 5000) {
                    return;
                }
                state.progressWriteAt = now;
                state.progress[state.currentPlaying.key] = Math.max(0, Number(payload.milliseconds) || 0);
                saveStateObject(STORAGE_PROGRESS, state.progress);
            });
        }
    }

    function bootstrap() {
        refreshSavedProfiles("");
        setMode("xtream");
        installEvents();
        if (!native) {
            status($("login-status"), "A ponte nativa do Windows não foi carregada.", true);
            $("connect").disabled = true;
        }
    }

    bootstrap();
}());
