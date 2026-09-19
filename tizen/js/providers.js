/* SPDX-License-Identifier: MIT */
/* Provider adapters for the Samsung Tizen Web port. */
(function () {
    "use strict";

    function hashText(text) {
        var hash = 2166136261;
        var i;
        text = String(text || "");
        for (i = 0; i < text.length; i += 1) {
            hash ^= text.charCodeAt(i);
            hash = (hash * 16777619) >>> 0;
        }
        return ("00000000" + hash.toString(16)).slice(-8);
    }

    function normalizeWords(text) {
        return String(text || "")
            .toLowerCase()
            .replace(/[áàâãä]/g, "a")
            .replace(/[éèêë]/g, "e")
            .replace(/[íìîï]/g, "i")
            .replace(/[óòôõö]/g, "o")
            .replace(/[úùûü]/g, "u")
            .replace(/ç/g, "c")
            .replace(/[^a-z0-9]+/g, " ")
            .replace(/^\s+|\s+$/g, "")
            .replace(/\s+/g, " ");
    }

    function hasWord(text, word) {
        return (" " + text + " ").indexOf(" " + word + " ") !== -1;
    }

    function classifyGroup(group) {
        var norm = normalizeWords(group);

        if (hasWord(norm, "series") || hasWord(norm, "serie") ||
                hasWord(norm, "seriados") || hasWord(norm, "seriado") ||
                hasWord(norm, "anime") || hasWord(norm, "animes") ||
                hasWord(norm, "novela") || hasWord(norm, "novelas") ||
                hasWord(norm, "dorama") || hasWord(norm, "doramas")) {
            return "series";
        }

        if (hasWord(norm, "filmes") || hasWord(norm, "filme") ||
                hasWord(norm, "movies") || hasWord(norm, "movie") ||
                hasWord(norm, "vod") || hasWord(norm, "cinema")) {
            return "vod";
        }

        return "live";
    }

    function parseEpisodeLabel(name) {
        var text = String(name || "");
        var match = /(?:^|[^A-Za-z0-9])(?:[sStT]\s*(\d{1,4})[\s._-]*[eE]\s*(\d{1,4})|(\d{1,4})[xX](\d{1,4}))/.exec(text);
        var season;
        var episode;
        var title;

        if (!match) {
            return null;
        }

        season = parseInt(match[1] || match[3], 10);
        episode = parseInt(match[2] || match[4], 10);
        title = text.slice(0, match.index).replace(/[\s._|:-]+$/g, "");
        title = title.replace(/(?:[\s._|:-]+)?(?:\(|\[)?(19\d{2}|20\d{2})(?:\)|\])?$/g, "");
        title = title.replace(/[\s._|:-]+$/g, "");

        if (!title) {
            return null;
        }

        return {
            title: title,
            season: season,
            episode: episode
        };
    }

    function parseAttribute(line, key) {
        var pattern = new RegExp("(?:^|[\\s,])" + key + "=(?:\"([^\"]*)\"|([^,\\s]*))", "i");
        var match = pattern.exec(line);
        return match ? (match[1] !== undefined ? match[1] : match[2]) : "";
    }

    function extinfName(line) {
        var quoted = false;
        var i;

        for (i = 0; i < line.length; i += 1) {
            if (line.charAt(i) === "\"") {
                quoted = !quoted;
            } else if (line.charAt(i) === "," && !quoted) {
                return line.slice(i + 1).replace(/^\s+|\s+$/g, "") || "Canal";
            }
        }
        return "Canal";
    }

    function resolveUrl(source, media) {
        var anchor;
        var origin;
        var slash;

        media = String(media || "").replace(/^\s+|\s+$/g, "");
        if (!media) {
            return "";
        }

        if (/^https?:\/\//i.test(media)) {
            return media;
        }

        if (/^\/\//.test(media)) {
            return (/^https:/i.test(source) ? "https:" : "http:") + media;
        }

        if (!/^https?:\/\//i.test(source)) {
            return media;
        }

        anchor = document.createElement("a");
        anchor.href = source;
        origin = anchor.protocol + "//" + anchor.host;

        if (media.charAt(0) === "/") {
            return origin + media;
        }

        slash = source.lastIndexOf("/");
        return (slash >= 8 ? source.slice(0, slash + 1) : origin + "/") + media;
    }

    function categoryId(group) {
        return "m3ug:" + hashText(group || "Sem grupo");
    }

    function makeM3uItem(source, pending, mediaUrl) {
        var name = pending.name || "Canal";
        var group = pending.group || "Sem grupo";
        var episode = parseEpisodeLabel(name);
        var kind = episode ? "series" : classifyGroup(group);
        var resolved = resolveUrl(source, mediaUrl);

        return {
            uid: "m3u:" + hashText(name + "|" + resolved),
            source: "m3u",
            kind: kind,
            name: name,
            group: group,
            categoryId: categoryId(group),
            categoryName: group,
            logo: pending.logo || "",
            url: resolved,
            episodeInfo: episode
        };
    }

    function parseM3u(text, source) {
        var catalogs = {
            live: { items: [], categories: [] },
            vod: { items: [], categories: [] },
            series: { items: [], categories: [] }
        };
        var categoryMaps = { live: {}, vod: {}, series: {} };
        var seriesMap = {};
        var pending = null;
        var cursor = 0;
        var next;

        function ensureCategory(kind, group) {
            var safeGroup = group || "Sem grupo";
            var id = categoryId(safeGroup);
            if (!categoryMaps[kind][id]) {
                categoryMaps[kind][id] = true;
                catalogs[kind].categories.push({ id: id, name: safeGroup });
            }
        }

        function consumeLine(raw) {
            var line = raw.replace(/\r$/, "").replace(/^\s+|\s+$/g, "");
            var item;
            var info;
            var key;
            var series;

            if (!line) {
                return;
            }

            if (line.indexOf("#EXTINF:") === 0) {
                pending = {
                    name: extinfName(line),
                    group: parseAttribute(line, "group-title") || "Sem grupo",
                    logo: parseAttribute(line, "tvg-logo")
                };
                return;
            }

            if (line.charAt(0) === "#") {
                return;
            }

            if (!pending) {
                pending = { name: "Canal", group: "Sem grupo", logo: "" };
            }

            item = makeM3uItem(source, pending, line);
            pending = null;

            if (!/^https?:\/\//i.test(item.url)) {
                return;
            }

            if (item.kind !== "series") {
                ensureCategory(item.kind, item.group);
                catalogs[item.kind].items.push(item);
                return;
            }

            info = item.episodeInfo;
            if (!info) {
                item.kind = "vod";
                ensureCategory("vod", item.group);
                catalogs.vod.items.push(item);
                return;
            }

            key = normalizeWords(info.title);

            if (!seriesMap[key]) {
                series = {
                    uid: "m3useries:" + hashText(key),
                    source: "m3u",
                    kind: "series",
                    name: info.title,
                    group: item.group,
                    categoryId: categoryId(item.group),
                    categoryName: item.group,
                    logo: item.logo,
                    episodes: []
                };
                seriesMap[key] = series;
                catalogs.series.items.push(series);
                ensureCategory("series", item.group);
            }

            series = seriesMap[key];
            if (!series.logo && item.logo) {
                series.logo = item.logo;
            }

            series.episodes.push({
                uid: item.uid,
                source: "m3u",
                kind: "episode",
                name: item.name,
                seriesName: info.title,
                season: info.season,
                episode: info.episode,
                group: item.group,
                logo: item.logo,
                url: item.url
            });
        }

        while (cursor <= text.length) {
            next = text.indexOf("\n", cursor);
            if (next === -1) {
                consumeLine(text.slice(cursor));
                break;
            }
            consumeLine(text.slice(cursor, next));
            cursor = next + 1;
        }

        catalogs.series.items.forEach(function (series) {
            series.episodes.sort(function (a, b) {
                if (a.season !== b.season) {
                    return a.season - b.season;
                }
                return a.episode - b.episode;
            });
        });

        return catalogs;
    }

    function loadM3u(url) {
        return window.BlazzingNet.text(url, {
            timeout: 60000,
            maxBytes: window.BlazzingNet.MAX_RESPONSE_BYTES
        }).then(function (text) {
            if (text.indexOf("#EXTM3U") === -1 && text.indexOf("#EXTINF:") === -1) {
                throw new Error("O conteúdo recebido não parece ser uma playlist M3U.");
            }
            return parseM3u(text, url);
        });
    }

    function normalizeServer(server) {
        server = String(server || "").replace(/^\s+|\s+$/g, "");
        if (!server) {
            return "";
        }
        if (!/^https?:\/\//i.test(server)) {
            server = "http://" + server;
        }
        return server.replace(/\/+$/, "") + "/";
    }

    function XtreamClient(profile) {
        this.username = profile.username || "";
        this.password = profile.password || "";
        this.servers = [];
        this.activeServer = "";
        this.cache = {};

        [profile.server, profile.alternate].forEach(function (server) {
            server = normalizeServer(server);
            if (server && this.servers.indexOf(server) === -1) {
                this.servers.push(server);
            }
        }, this);
    }

    XtreamClient.prototype.apiUrl = function (server, action, extra) {
        var url = server + "player_api.php?username=" + encodeURIComponent(this.username) +
            "&password=" + encodeURIComponent(this.password);
        var key;

        if (action) {
            url += "&action=" + encodeURIComponent(action);
        }

        extra = extra || {};
        for (key in extra) {
            if (Object.prototype.hasOwnProperty.call(extra, key)) {
                url += "&" + encodeURIComponent(key) + "=" + encodeURIComponent(extra[key]);
            }
        }

        return url;
    };

    XtreamClient.prototype.tryServers = function (factory) {
        var self = this;
        var ordered = this.activeServer ?
            [this.activeServer].concat(this.servers.filter(function (server) {
                return server !== self.activeServer;
            })) :
            this.servers.slice();

        function attempt(index, lastError) {
            if (index >= ordered.length) {
                throw lastError || new Error("Nenhum servidor Xtream disponível.");
            }

            return factory(ordered[index]).then(function (value) {
                self.activeServer = ordered[index];
                return value;
            }).catch(function (error) {
                return attempt(index + 1, error);
            });
        }

        if (!ordered.length) {
            return Promise.reject(new Error("Informe ao menos um servidor Xtream."));
        }

        return attempt(0, null);
    };

    XtreamClient.prototype.authenticate = function () {
        var self = this;

        return this.tryServers(function (server) {
            return window.BlazzingNet.json(self.apiUrl(server, "", {}), { timeout: 25000 })
                .then(function (payload) {
                    var user = payload && payload.user_info;
                    var active = user && (
                        user.auth === 1 ||
                        user.auth === "1" ||
                        String(user.status || "").toLowerCase() === "active"
                    );

                    if (!active) {
                        throw new Error("O servidor rejeitou as credenciais.");
                    }

                    return payload;
                });
        });
    };

    XtreamClient.prototype.action = function (action, extra) {
        var self = this;
        return this.tryServers(function (server) {
            return window.BlazzingNet.json(
                self.apiUrl(server, action, extra),
                { timeout: 45000, maxBytes: window.BlazzingNet.MAX_RESPONSE_BYTES }
            );
        });
    };

    XtreamClient.prototype.streamUrl = function (kind, id, extension) {
        var folder = kind === "live" ? "live" : (kind === "vod" ? "movie" : "series");
        var ext = extension || (kind === "live" ? "ts" : "mp4");

        return this.activeServer + folder + "/" +
            encodeURIComponent(this.username) + "/" +
            encodeURIComponent(this.password) + "/" +
            encodeURIComponent(id) + "." +
            String(ext).replace(/^\./, "");
    };

    XtreamClient.prototype.load = function (kind) {
        var self = this;
        var categoryAction;
        var streamAction;

        if (this.cache[kind]) {
            return Promise.resolve(this.cache[kind]);
        }

        categoryAction = kind === "live" ? "get_live_categories" :
            (kind === "vod" ? "get_vod_categories" : "get_series_categories");
        streamAction = kind === "live" ? "get_live_streams" :
            (kind === "vod" ? "get_vod_streams" : "get_series");

        return Promise.all([
            this.action(categoryAction),
            this.action(streamAction)
        ]).then(function (parts) {
            var categoriesRaw = Array.isArray(parts[0]) ? parts[0] : [];
            var streamsRaw = Array.isArray(parts[1]) ? parts[1] : [];
            var categoryMap = {};
            var catalog = { categories: [], items: [] };

            categoriesRaw.forEach(function (category) {
                var id = String(category.category_id || "");
                categoryMap[id] = category.category_name || "Sem categoria";
                catalog.categories.push({
                    id: id,
                    name: categoryMap[id]
                });
            });

            streamsRaw.forEach(function (entry) {
                var id;
                var categoryValue = String(entry.category_id || "");
                var item;

                if (kind === "series") {
                    id = String(entry.series_id || "");
                    item = {
                        uid: "xtream:series:" + self.activeServer + ":" + id,
                        source: "xtream",
                        kind: "series",
                        id: id,
                        name: entry.name || "Série",
                        logo: entry.cover || entry.stream_icon || "",
                        categoryId: categoryValue,
                        categoryName: categoryMap[categoryValue] || "Sem categoria"
                    };
                } else {
                    id = String(entry.stream_id || "");
                    item = {
                        uid: "xtream:" + kind + ":" + self.activeServer + ":" + id,
                        source: "xtream",
                        kind: kind,
                        id: id,
                        name: entry.name || "Item",
                        logo: entry.stream_icon || "",
                        categoryId: categoryValue,
                        categoryName: categoryMap[categoryValue] || "Sem categoria",
                        url: self.streamUrl(kind, id, entry.container_extension)
                    };
                }

                catalog.items.push(item);
            });

            self.cache[kind] = catalog;
            return catalog;
        });
    };

    XtreamClient.prototype.seriesInfo = function (series) {
        var self = this;

        return this.action("get_series_info", { series_id: series.id }).then(function (payload) {
            var episodesObject = payload && payload.episodes ? payload.episodes : {};
            var seasons = [];
            var episodes = [];
            var seasonKey;

            for (seasonKey in episodesObject) {
                if (Object.prototype.hasOwnProperty.call(episodesObject, seasonKey)) {
                    seasons.push(parseInt(seasonKey, 10) || 0);

                    (episodesObject[seasonKey] || []).forEach(function (episode) {
                        var id = String(episode.id || episode.episode_id || "");
                        var seasonNumber = parseInt(episode.season || seasonKey, 10) || 0;
                        var episodeNumber = parseInt(
                            episode.episode_num || episode.episode || 0,
                            10
                        ) || 0;

                        episodes.push({
                            uid: "xtream:episode:" + self.activeServer + ":" + id,
                            source: "xtream",
                            kind: "episode",
                            id: id,
                            name: episode.title || episode.name || ("Episódio " + episodeNumber),
                            seriesName: series.name,
                            season: seasonNumber,
                            episode: episodeNumber,
                            logo: series.logo || "",
                            url: self.streamUrl("series", id, episode.container_extension)
                        });
                    });
                }
            }

            seasons.sort(function (a, b) { return a - b; });
            episodes.sort(function (a, b) {
                if (a.season !== b.season) {
                    return a.season - b.season;
                }
                return a.episode - b.episode;
            });

            return {
                title: series.name,
                seasons: seasons,
                episodes: episodes,
                info: payload && payload.info ? payload.info : {}
            };
        });
    };

    window.BlazzingProviders = {
        hashText: hashText,
        classifyGroup: classifyGroup,
        parseEpisodeLabel: parseEpisodeLabel,
        loadM3u: loadM3u,
        parseM3u: parseM3u,
        XtreamClient: XtreamClient
    };
}());
