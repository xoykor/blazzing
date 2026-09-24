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

    function stableHash32(text, seed) {
        var hash = seed | 0;
        var i;
        text = String(text || "");
        for (i = 0; i < text.length; i += 1) {
            hash = (((hash << 5) - hash) + text.charCodeAt(i)) | 0;
        }
        return ("00000000" + (hash >>> 0).toString(16)).slice(-8);
    }

    function cardLookupKey(name, group) {
        var value = String(group || "") + "\u0000" + String(name || "");
        return stableHash32(value, 0x13579bdf) +
            stableHash32(value, 0x2468ace1);
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
                hasWord(norm, "dorama") || hasWord(norm, "doramas") ||
                hasWord(norm, "temporada") || hasWord(norm, "temporadas")) {
            return "series";
        }

        if (hasWord(norm, "filmes") || hasWord(norm, "filme") ||
                hasWord(norm, "movies") || hasWord(norm, "movie") ||
                hasWord(norm, "vod") || hasWord(norm, "cinema") ||
                hasWord(norm, "lancamentos") || hasWord(norm, "lancamento")) {
            return "vod";
        }

        return "live";
    }

    function inferKind(group, name, mediaUrl, episode) {
        var groupKind = classifyGroup(group);
        var norm = normalizeWords(group);
        var url = String(mediaUrl || "").toLowerCase().split("?")[0];
        var vodExtension = /\.(mp4|mkv|avi|mov|m4v|webm)$/.test(url);

        if (episode) {
            return "series";
        }

        if (groupKind !== "live") {
            return groupKind;
        }

        if (vodExtension &&
                !hasWord(norm, "tv") &&
                !hasWord(norm, "canais") &&
                !hasWord(norm, "canal") &&
                !hasWord(norm, "ao vivo") &&
                !hasWord(norm, "live")) {
            return "vod";
        }

        return "live";
    }

    function cleanCategoryName(group, kind) {
        var value = String(group || "")
            .replace(/^\s+|\s+$/g, "")
            .replace(/\s+/g, " ");

        if (!value || normalizeWords(value) === "sem grupo") {
            return "Outros";
        }

        value = value
            .replace(/^\s*(?:\[[^\]]+\]\s*)+/g, "")
            .replace(/^\s*(?:BR\s*[-|:]\s*)/i, "")
            .replace(/^\s*(?:CANAIS?|TV|AO\s*VIVO|LIVE)\s*[-|:»>]\s*/i, "")
            .replace(/^\s*(?:FILMES?|MOVIES?|VOD|CINEMA)\s*[-|:»>]\s*/i, "")
            .replace(/^\s*(?:S[ÉE]RIES?|SERIADOS?|ANIMES?|NOVELAS?|DORAMAS?)\s*[-|:»>]\s*/i, "")
            .replace(/^\s+|\s+$/g, "");

        if (!value) {
            return kind === "vod" ? "Filmes" :
                (kind === "series" ? "Séries" : "TV");
        }

        return value;
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

    function categoryId(group, kind) {
        return "m3ug:" + hashText((kind || "") + "|" + normalizeWords(group || "Outros"));
    }

    function makeM3uItem(source, pending, mediaUrl, classified) {
        classified = classified || {};
        var name = pending.name || "Canal";
        var rawGroup = pending.group || "Sem grupo";
        var episode = classified.episode !== undefined ?
            classified.episode : parseEpisodeLabel(name);
        var resolved = classified.resolved || resolveUrl(source, mediaUrl);
        var kind = classified.kind || inferKind(rawGroup, name, resolved, episode);
        var group = cleanCategoryName(rawGroup, kind);

        return {
            uid: "m3u:" + hashText(name + "|" + resolved),
            source: "m3u",
            kind: kind,
            name: name,
            group: group,
            rawGroup: rawGroup,
            categoryId: categoryId(group, kind),
            categoryName: group,
            logo: pending.logo ? resolveUrl(source, pending.logo) : "",
            cardKey: pending.cardIndexBase && !pending.logo ?
                cardLookupKey(name, rawGroup) : "",
            cardIndexBase: pending.cardIndexBase || "",
            cardIndexVersion: pending.cardIndexVersion || "",
            cardIndexShardLength: pending.cardIndexShardLength || 1,
            fallbackId: pending.fallbackId || "",
            fallbackIndexBase: pending.fallbackIndexBase || "",
            fallbackIndexVersion: pending.fallbackIndexVersion || "",
            fallbackIndexShardLength: pending.fallbackIndexShardLength || 2,
            url: resolved,
            episodeInfo: episode
        };
    }

    function createM3uParser(source, options) {
        options = options || {};
        var onlyKind = String(options.onlyKind || "");
        var seriesSummaryOnly = !!options.seriesSummaryOnly;
        var seriesFilterKey = normalizeWords(options.seriesFilterKey || "");
        var catalogs = {
            live: { items: [], categories: [] },
            vod: { items: [], categories: [] },
            series: { items: [], categories: [] }
        };
        var categoryMaps = { live: {}, vod: {}, series: {} };
        var seriesMap = {};
        var pending = null;
        var cardIndexBase = "";
        var cardIndexVersion = "";
        var cardIndexShardLength = 1;
        var fallbackIndexBase = "";
        var fallbackIndexVersion = "";
        var fallbackIndexShardLength = 2;
        var lineCarry = "";
        var hasM3uMarker = false;

        function ensureCategory(kind, group) {
            var safeGroup = cleanCategoryName(group, kind);
            var id = categoryId(safeGroup, kind);
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
            var entryName;
            var rawGroup;
            var resolved;
            var inferredKind;

            if (!line) {
                return;
            }

            if (line.indexOf("#EXTM3U") === 0 || line.indexOf("#EXTINF:") === 0) {
                hasM3uMarker = true;
            }

            if (line.indexOf("#EXT-X-LISTA-FALLBACK:") === 0) {
                fallbackIndexBase = line.slice("#EXT-X-LISTA-FALLBACK:".length)
                    .replace(/^\s+|\s+$/g, "");
                return;
            }

            if (line.indexOf("#EXT-X-LISTA-FALLBACK-VERSION:") === 0) {
                fallbackIndexVersion = line.slice("#EXT-X-LISTA-FALLBACK-VERSION:".length)
                    .replace(/^\s+|\s+$/g, "");
                return;
            }

            if (line.indexOf("#EXT-X-LISTA-FALLBACK-SHARD-LEN:") === 0) {
                var parsedFallbackShardLength = parseInt(
                    line.slice("#EXT-X-LISTA-FALLBACK-SHARD-LEN:".length),
                    10
                );
                if (parsedFallbackShardLength >= 1 && parsedFallbackShardLength <= 4) {
                    fallbackIndexShardLength = parsedFallbackShardLength;
                }
                return;
            }

            if (line.indexOf("#EXT-X-LISTA-CARDS:") === 0) {
                cardIndexBase = line.slice("#EXT-X-LISTA-CARDS:".length)
                    .replace(/^\s+|\s+$/g, "");
                return;
            }

            if (line.indexOf("#EXT-X-LISTA-CARDS-VERSION:") === 0) {
                cardIndexVersion = line.slice("#EXT-X-LISTA-CARDS-VERSION:".length)
                    .replace(/^\s+|\s+$/g, "");
                return;
            }

            if (line.indexOf("#EXT-X-LISTA-CARDS-SHARD-LEN:") === 0) {
                var parsedShardLength = parseInt(
                    line.slice("#EXT-X-LISTA-CARDS-SHARD-LEN:".length),
                    10
                );
                if (parsedShardLength >= 1 && parsedShardLength <= 4) {
                    cardIndexShardLength = parsedShardLength;
                }
                return;
            }

            if (line.indexOf("#EXTINF:") === 0) {
                pending = {
                    name: extinfName(line),
                    group: parseAttribute(line, "group-title") || "Sem grupo",
                    logo: parseAttribute(line, "tvg-logo"),
                    fallbackId: parseAttribute(line, "x-lista-fallback"),
                    fallbackIndexBase: fallbackIndexBase,
                    fallbackIndexVersion: fallbackIndexVersion,
                    fallbackIndexShardLength: fallbackIndexShardLength,
                    cardIndexBase: cardIndexBase,
                    cardIndexVersion: cardIndexVersion,
                    cardIndexShardLength: cardIndexShardLength
                };
                return;
            }

            if (line.charAt(0) === "#") {
                return;
            }

            if (!pending) {
                pending = {
                    name: "Canal",
                    group: "Sem grupo",
                    logo: "",
                    fallbackId: "",
                    fallbackIndexBase: fallbackIndexBase,
                    fallbackIndexVersion: fallbackIndexVersion,
                    fallbackIndexShardLength: fallbackIndexShardLength,
                    cardIndexBase: cardIndexBase,
                    cardIndexVersion: cardIndexVersion,
                    cardIndexShardLength: cardIndexShardLength
                };
            }

            entryName = pending.name || "Canal";
            rawGroup = pending.group || "Sem grupo";
            info = parseEpisodeLabel(entryName);
            resolved = resolveUrl(source, line);
            inferredKind = inferKind(rawGroup, entryName, resolved, info);

            /*
             * Alguns grupos são rotulados como Séries mesmo quando a entrada
             * não representa um episódio identificável. O parser antigo
             * tratava esses casos como VOD. Preserve essa regra antes do
             * filtro por seção para nunca tentar acessar info.title com
             * episodeInfo nulo.
             */
            if (inferredKind === "series" && !info) {
                inferredKind = "vod";
            }

            if (!/^https?:\/\//i.test(resolved)) {
                pending = null;
                return;
            }

            /*
             * O catálogo Tizen é carregado por seção. Classifique primeiro e
             * só materialize o objeto completo quando a entrada pertence à
             * seção solicitada. Isso evita centenas de milhares de objetos
             * temporários ao abrir uma M3U muito grande.
             */
            if (onlyKind && inferredKind !== onlyKind) {
                pending = null;
                return;
            }

            if (inferredKind === "series") {
                key = normalizeWords(info && info.title || entryName);
                if (seriesFilterKey && key !== seriesFilterKey) {
                    pending = null;
                    return;
                }

                if (seriesSummaryOnly && seriesMap[key]) {
                    seriesMap[key].episodeCount += 1;
                    pending = null;
                    return;
                }
            }

            item = makeM3uItem(source, pending, line, {
                episode: info,
                resolved: resolved,
                kind: inferredKind
            });
            pending = null;

            if (item.kind !== "series") {
                ensureCategory(item.kind, item.group);
                catalogs[item.kind].items.push(item);
                return;
            }

            info = item.episodeInfo;
            key = normalizeWords(info.title);

            if (!seriesMap[key]) {
                series = {
                    uid: "m3useries:" + hashText(key),
                    source: "m3u",
                    kind: "series",
                    name: info.title,
                    group: item.group,
                    categoryId: categoryId(item.group, "series"),
                    categoryName: item.group,
                    logo: item.logo,
                    cardKey: item.cardKey || "",
                    cardIndexBase: item.cardIndexBase || "",
                    cardIndexVersion: item.cardIndexVersion || "",
                    cardIndexShardLength: item.cardIndexShardLength || 1,
                    seriesKey: key,
                    episodeCount: 0,
                    episodes: seriesSummaryOnly ? null : []
                };
                seriesMap[key] = series;
                catalogs.series.items.push(series);
                ensureCategory("series", item.group);
            }

            series = seriesMap[key];
            series.episodeCount += 1;
            if (!series.logo && item.logo) {
                series.logo = item.logo;
            }
            if (!series.cardKey && item.cardKey) {
                series.cardKey = item.cardKey;
                series.cardIndexBase = item.cardIndexBase || "";
                series.cardIndexVersion = item.cardIndexVersion || "";
                series.cardIndexShardLength = item.cardIndexShardLength || 1;
            }

            if (seriesSummaryOnly) {
                return;
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
                cardKey: item.cardKey || "",
                cardIndexBase: item.cardIndexBase || "",
                cardIndexVersion: item.cardIndexVersion || "",
                cardIndexShardLength: item.cardIndexShardLength || 1,
                fallbackId: item.fallbackId || "",
                fallbackIndexBase: item.fallbackIndexBase || "",
                fallbackIndexVersion: item.fallbackIndexVersion || "",
                fallbackIndexShardLength: item.fallbackIndexShardLength || 2,
                url: item.url
            });
        }

        function consumeTextChunk(chunk) {
            var text = lineCarry + String(chunk || "");
            var cursor = 0;
            var next;

            while (cursor < text.length) {
                next = text.indexOf("\n", cursor);
                if (next === -1) {
                    lineCarry = text.slice(cursor);
                    return;
                }
                consumeLine(text.slice(cursor, next));
                cursor = next + 1;
            }
            lineCarry = "";
        }

        function finish() {
            if (lineCarry) {
                consumeLine(lineCarry);
                lineCarry = "";
            }

            catalogs.series.items.forEach(function (series) {
                if (!Array.isArray(series.episodes)) {
                    return;
                }
                series.episodes.sort(function (a, b) {
                    if (a.season !== b.season) {
                        return a.season - b.season;
                    }
                    return a.episode - b.episode;
                });
            });

            ["live", "vod", "series"].forEach(function (kind) {
                catalogs[kind].categories.sort(function (a, b) {
                    return String(a.name || "").localeCompare(
                        String(b.name || ""),
                        "pt-BR",
                        { sensitivity: "base", numeric: true }
                    );
                });

                /* Keep provider order for items. Sorting huge IPTV catalogs here
                 * causes long UI stalls on televisions; categories are sorted
                 * separately and filtering keeps item order stable. */
            });

            return catalogs;
        }

        return {
            consumeTextChunk: consumeTextChunk,
            finish: finish,
            hasM3uMarker: function () { return hasM3uMarker; }
        };
    }

    function parseM3u(text, source) {
        var parser = createM3uParser(source);
        parser.consumeTextChunk(text);
        return parser.finish();
    }

    function parseM3uAsync(text, source, options) {
        options = options || {};
        text = String(text || "");

        var parser = createM3uParser(source, options);
        var cursor = 0;
        var chunkChars = Math.max(
            32 * 1024,
            Math.min(1024 * 1024, options.chunkChars || 256 * 1024)
        );

        return new Promise(function (resolve, reject) {
            function step() {
                var end;

                try {
                    if (cursor >= text.length) {
                        resolve(parser.finish());
                        return;
                    }

                    end = Math.min(text.length, cursor + chunkChars);
                    parser.consumeTextChunk(text.slice(cursor, end));
                    cursor = end;

                    if (options.onProgress) {
                        options.onProgress(cursor, text.length);
                    }
                } catch (error) {
                    reject(error);
                    return;
                }

                setTimeout(step, 0);
            }

            setTimeout(step, 0);
        });
    }

    function parseDownloadedM3u(text, url) {
        if (text.indexOf("#EXTM3U") === -1 && text.indexOf("#EXTINF:") === -1) {
            throw new Error("O conteúdo recebido não parece ser uma playlist M3U.");
        }
        return parseM3u(text, url);
    }

    function downloadM3u(url) {
        return window.BlazzingNet.text(url, {
            timeout: 60000,
            maxBytes: window.BlazzingNet.MAX_RESPONSE_BYTES
        }).then(function (text) {
            if (text.indexOf("#EXTM3U") === -1 && text.indexOf("#EXTINF:") === -1) {
                throw new Error("O conteúdo recebido não parece ser uma playlist M3U.");
            }
            return parseM3uAsync(text, url).then(function (catalogs) {
                return {
                    text: text,
                    catalogs: catalogs
                };
            });
        });
    }

    function loadM3u(url) {
        return downloadM3u(url).then(function (result) {
            return result.catalogs;
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
            var categoryIdMap = {};
            var categoryByName = {};
            var catalog = { categories: [], items: [] };

            categoriesRaw.forEach(function (category) {
                var originalId = String(category.category_id || "");
                var cleaned = cleanCategoryName(
                    category.category_name || "Sem categoria",
                    kind
                );
                var normalized = normalizeWords(cleaned);
                var canonicalId;

                if (!categoryByName[normalized]) {
                    canonicalId = originalId || ("xtream:" + hashText(kind + "|" + normalized));
                    categoryByName[normalized] = canonicalId;
                    catalog.categories.push({
                        id: canonicalId,
                        name: cleaned
                    });
                } else {
                    canonicalId = categoryByName[normalized];
                }

                categoryMap[originalId] = cleaned;
                categoryIdMap[originalId] = canonicalId;
            });

            streamsRaw.forEach(function (entry) {
                var id;
                var rawCategoryValue = String(entry.category_id || "");
                var categoryValue = categoryIdMap[rawCategoryValue] || rawCategoryValue;
                var item;

                if (kind === "series") {
                    id = String(entry.series_id || "");
                    item = {
                        uid: "xtream:series:" + self.activeServer + ":" + id,
                        source: "xtream",
                        kind: "series",
                        id: id,
                        name: entry.name || "Série",
                        logo: resolveUrl(self.activeServer, entry.cover || entry.stream_icon || ""),
                        categoryId: categoryValue,
                        categoryName: categoryMap[rawCategoryValue] || "Outros"
                    };
                } else {
                    id = String(entry.stream_id || "");
                    item = {
                        uid: "xtream:" + kind + ":" + self.activeServer + ":" + id,
                        source: "xtream",
                        kind: kind,
                        id: id,
                        name: entry.name || "Item",
                        logo: resolveUrl(self.activeServer, entry.stream_icon || ""),
                        categoryId: categoryValue,
                        categoryName: categoryMap[rawCategoryValue] || "Outros",
                        url: self.streamUrl(kind, id, entry.container_extension)
                    };
                }

                catalog.items.push(item);
            });

            catalog.categories.sort(function (a, b) {
                return String(a.name || "").localeCompare(
                    String(b.name || ""),
                    "pt-BR",
                    { sensitivity: "base", numeric: true }
                );
            });

            /* Preserve the ordering supplied by the Xtream server. */
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
        cardLookupKey: cardLookupKey,
        normalizeWords: normalizeWords,
        classifyGroup: classifyGroup,
        cleanCategoryName: cleanCategoryName,
        inferKind: inferKind,
        parseEpisodeLabel: parseEpisodeLabel,
        loadM3u: loadM3u,
        downloadM3u: downloadM3u,
        createM3uParser: createM3uParser,
        parseM3u: parseM3u,
        parseM3uAsync: parseM3uAsync,
        XtreamClient: XtreamClient
    };
}());
