(function (global) {
  "use strict";

  var activeScreen = "home";
  var pairingSession = null;
  var catalog = null;
  var selectedGroup = "";
  var playerReturnScreen = "home";
  var xtreamSession = null;
  var catalogPage = 0;
  var catalogPageSize = 48;
  var catalogQuery = "";
  var catalogStack = [];
  var catalogRequestId = 0;
  var catalogFocusRequest = "";
  var activeProgressKey = "";
  var activePlayback = null;
  var vodEntry = null;
  var seriesEntry = null;
  var seriesPayload = null;
  var FAVORITES_GROUP = "__favorites__";

  function byId(id) {
    return document.getElementById(id);
  }

  function showScreen(name) {
    var screens = document.querySelectorAll(".screen");
    var i;

    for (i = 0; i < screens.length; i += 1) {
      screens[i].classList.remove("is-active");
    }

    byId("screen-" + name).classList.add("is-active");
    activeScreen = name;

    setTimeout(function () {
      global.BlazzingNavigation.focusFirst();
    }, 0);
  }

  function stopPairing() {
    if (!pairingSession) {
      return Promise.resolve();
    }

    var session = pairingSession;
    pairingSession = null;
    return session.stop();
  }

  function savePlayerProgress() {
    var position;

    if (!activeProgressKey) {
      return;
    }

    position = global.BlazzingPlayer.position();
    global.BlazzingStorage.setProgress(
      activeProgressKey,
      position.currentTime,
      position.duration
    );
  }

  function releaseRemoteCatalog() {
    if (catalog && catalog.lazyM3U && catalog.sessionId) {
      global.BlazzingNetwork.releaseM3U(catalog.sessionId);
    }
  }

  function releaseDetailArtwork() {
    global.BlazzingArtwork.releaseImage(byId("vod-poster"));
    global.BlazzingArtwork.releaseImage(byId("series-poster"));
  }

  function showHome() {
    stopPairing();
    releaseDetailArtwork();
    savePlayerProgress();
    global.BlazzingPlayer.stop();
    activeProgressKey = "";
    activePlayback = null;
    vodEntry = null;
    seriesEntry = null;
    seriesPayload = null;
    releaseRemoteCatalog();
    catalog = null;
    catalogStack = [];
    catalogRequestId += 1;
    xtreamSession = null;
    byId("xtream-password").value = "";
    showScreen("home");
  }

  function restoreXtreamProfile() {
    var profile = global.BlazzingStorage.getXtreamProfile();

    if (!profile) {
      byId("xtream-remember").checked = false;
      return;
    }

    byId("xtream-server").value = profile.server;
    byId("xtream-username").value = profile.username;
    byId("xtream-remember").checked = true;
  }

  function validHttpUrl(value) {
    return /^https?:\/\//i.test(value);
  }

  function formatTime(seconds) {
    var total = Math.max(0, Math.floor(Number(seconds || 0)));
    var hours = Math.floor(total / 3600);
    var minutes = Math.floor((total % 3600) / 60);
    var secs = total % 60;

    if (hours > 0) {
      return hours + ":" +
        (minutes < 10 ? "0" : "") + minutes + ":" +
        (secs < 10 ? "0" : "") + secs;
    }

    return minutes + ":" + (secs < 10 ? "0" : "") + secs;
  }

  function setRetryVisible(visible) {
    var button = byId("player-retry");

    button.style.display = visible ? "" : "none";
    button.disabled = !visible;
  }

  function playbackSources(url, entry) {
    var sources = [];
    var fallback = entry && entry.fallbackUrl ?
      String(entry.fallbackUrl) : "";

    if (validHttpUrl(url)) {
      sources.push(url);
    }

    if (validHttpUrl(fallback) && fallback !== url) {
      sources.push(fallback);
    }

    return sources;
  }

  function currentResumePoint(playback) {
    var stored;

    if (!playback) {
      return 0;
    }

    if (activeProgressKey) {
      stored = global.BlazzingStorage.getProgress(activeProgressKey);
      if (stored >= 10) {
        return stored;
      }
    }

    return Number(playback.resumeAt || 0);
  }

  function openPlaybackCandidate(playback) {
    var resumeAt;
    var sourceIndex;

    if (!playback || activePlayback !== playback) {
      return;
    }

    sourceIndex = Number(playback.sourceIndex || 0);
    if (sourceIndex < 0 || sourceIndex >= playback.sources.length) {
      sourceIndex = 0;
      playback.sourceIndex = 0;
    }

    resumeAt = currentResumePoint(playback);
    setRetryVisible(false);

    global.BlazzingPlayer.open(playback.sources[sourceIndex], {
      resumeAt: resumeAt,
      onResume: function (seconds) {
        if (activePlayback !== playback) {
          return;
        }

        byId("player-status").textContent =
          "Retomando em " + formatTime(seconds) + "…";
      },
      onPlaying: function () {
        var suffix = playback.sourceIndex > 0 ?
          " • fonte alternativa" : "";

        if (activePlayback !== playback) {
          return;
        }

        byId("player-status").textContent =
          resumeAt >= 10 ?
            "Reproduzindo • retomado em " + formatTime(resumeAt) + suffix :
            "Reproduzindo" + suffix;
      },
      onProgress: function (seconds, duration) {
        if (activePlayback !== playback) {
          return;
        }

        if (activeProgressKey) {
          global.BlazzingStorage.setProgress(
            activeProgressKey,
            seconds,
            duration
          );
        }
      },
      onEnded: function () {
        if (activePlayback !== playback) {
          return;
        }

        if (activeProgressKey) {
          global.BlazzingStorage.clearProgress(activeProgressKey);
          activeProgressKey = "";
        }
      },
      onError: function (message) {
        var nextIndex;

        if (activePlayback !== playback) {
          return;
        }

        nextIndex = playback.sourceIndex + 1;
        if (nextIndex < playback.sources.length) {
          playback.sourceIndex = nextIndex;
          playback.resumeAt = currentResumePoint(playback);
          byId("player-status").textContent =
            "Fonte principal falhou. Tentando rota alternativa…";
          global.BlazzingPlayer.stop();

          setTimeout(function () {
            openPlaybackCandidate(playback);
          }, 120);
          return;
        }

        byId("player-status").textContent =
          (message || "Falha de reprodução.") +
          " Você pode tentar novamente.";
        setRetryVisible(true);
      }
    });
  }

  function playUrl(url, title, returnScreen, entry) {
    var resumeAt = 0;
    var trackProgress = entry &&
      (entry.kind === "vod" ||
       entry.kind === "episode" ||
       entry.kind === "pluto-movie" ||
       entry.kind === "pluto-episode") &&
      entry.favoriteKey;
    var sources = playbackSources(url, entry);

    if (!sources.length) {
      return;
    }

    activeProgressKey = trackProgress ? entry.favoriteKey : "";
    if (activeProgressKey) {
      resumeAt = global.BlazzingStorage.getProgress(activeProgressKey);
    }

    activePlayback = {
      sources: sources,
      sourceIndex: 0,
      resumeAt: resumeAt,
      title: title || "Stream",
      returnScreen: returnScreen || "home",
      entry: entry || null
    };

    playerReturnScreen = activePlayback.returnScreen;
    byId("player-title").textContent = activePlayback.title;
    byId("player-status").textContent =
      resumeAt >= 10 ?
        "Retomando em " + formatTime(resumeAt) + "…" :
        "Carregando…";
    setRetryVisible(false);
    showScreen("player");
    openPlaybackCandidate(activePlayback);
  }

  function showVodMetadata(entry, metadata) {
    var image = byId("vod-poster");
    var details = [];
    var logo = String(metadata.logo || entry.logo || "");

    vodEntry = entry;
    byId("vod-title").textContent = metadata.title || entry.title || "Filme";
    byId("vod-plot").textContent =
      metadata.plot || "Sem sinopse fornecida pelo provider.";

    if (metadata.year) {
      details.push(metadata.year);
    }
    if (metadata.genre) {
      details.push(metadata.genre);
    }
    if (metadata.rating) {
      details.push("Nota " + metadata.rating);
    }
    if (metadata.duration) {
      details.push(metadata.duration);
    }

    byId("vod-meta").textContent = details.join(" • ") || entry.group || "";
    global.BlazzingArtwork.releaseImage(image);
    image.removeAttribute("src");
    image.style.display = "none";

    if (/^https?:\/\//i.test(logo)) {
      image.onload = function () {
        image.style.display = "block";
      };
      image.onerror = function () {
        global.BlazzingArtwork.invalidate(logo);
        global.BlazzingArtwork.releaseImage(image);
        image.style.display = "none";
      };
      global.BlazzingArtwork.apply(image, logo);
    }

    showScreen("vod");
  }

  function openVod(entry) {
    var fallback;

    if (entry && entry.kind === "pluto-movie") {
      fallback = global.BlazzingPluto.buildVodMetadata(entry);
      showVodMetadata(entry, fallback);
      byId("vod-status").textContent = "";
      return;
    }

    fallback = global.BlazzingXtream.buildVodMetadata({}, entry);
    showVodMetadata(entry, fallback);
    byId("vod-status").textContent = "Carregando detalhes…";

    if (!xtreamSession || !entry || !entry.vodId) {
      byId("vod-status").textContent = "";
      return;
    }

    global.BlazzingNetwork.xtreamRequest(
      xtreamSession,
      "get_vod_info",
      { vodId: entry.vodId }
    ).then(function (payload) {
      showVodMetadata(
        entry,
        global.BlazzingXtream.buildVodMetadata(payload, entry)
      );
      byId("vod-status").textContent = "";
    }).catch(function () {
      byId("vod-status").textContent =
        "Provider não retornou detalhes; a reprodução continua disponível.";
    });
  }

  function showSeriesMetadata(entry, metadata) {
    var image = byId("series-poster");
    var details = [];
    var logo = String(metadata.logo || entry.logo || "");

    seriesEntry = entry;
    byId("series-title").textContent =
      metadata.title || entry.title || "Série";
    byId("series-plot").textContent =
      metadata.plot || "Sem sinopse fornecida pelo provider.";

    if (metadata.year) {
      details.push(metadata.year);
    }
    if (metadata.genre) {
      details.push(metadata.genre);
    }
    if (metadata.rating) {
      details.push("Nota " + metadata.rating);
    }
    if (metadata.duration) {
      var durationText = String(metadata.duration);
      details.push(/[A-Za-z]/.test(durationText) ?
        durationText :
        durationText + " min");
    }

    byId("series-meta").textContent =
      details.join(" • ") || entry.group || "";

    global.BlazzingArtwork.releaseImage(image);
    image.removeAttribute("src");
    image.style.display = "none";

    if (/^https?:\/\//i.test(logo)) {
      image.onload = function () {
        image.style.display = "block";
      };
      image.onerror = function () {
        global.BlazzingArtwork.invalidate(logo);
        global.BlazzingArtwork.releaseImage(image);
        image.style.display = "none";
      };
      global.BlazzingArtwork.apply(image, logo);
    }

    showScreen("series");
  }

  function openSeriesDetails(entry) {
    var fallback;

    seriesPayload = null;
    byId("series-episodes").disabled = true;

    if (entry && entry.kind === "pluto-series") {
      fallback = global.BlazzingPluto.buildSeriesMetadata({}, entry);
      showSeriesMetadata(entry, fallback);
      byId("series-status").textContent = "Carregando temporadas…";

      global.BlazzingNetwork.plutoSeries(entry.plutoSeriesId)
        .then(function (payload) {
          if (seriesEntry !== entry) {
            return;
          }

          seriesPayload = payload;
          showSeriesMetadata(
            entry,
            global.BlazzingPluto.buildSeriesMetadata(payload, entry)
          );
          byId("series-status").textContent = "";
          byId("series-episodes").disabled = false;
        })
        .catch(function (error) {
          if (seriesEntry !== entry) {
            return;
          }

          byId("series-status").textContent =
            error && error.message ?
              error.message :
              "Falha ao carregar temporadas da Pluto TV.";
          byId("series-episodes").disabled = true;
        });
      return;
    }

    fallback = global.BlazzingXtream.buildSeriesMetadata({}, entry);
    showSeriesMetadata(entry, fallback);
    byId("series-status").textContent = "Carregando detalhes…";

    if (!xtreamSession || !entry || !entry.seriesId) {
      byId("series-status").textContent =
        "Não há detalhes adicionais disponíveis.";
      return;
    }

    global.BlazzingNetwork.xtreamRequest(
      xtreamSession,
      "get_series_info",
      { seriesId: entry.seriesId }
    ).then(function (payload) {
      seriesPayload = payload;
      showSeriesMetadata(
        entry,
        global.BlazzingXtream.buildSeriesMetadata(payload, entry)
      );
      byId("series-status").textContent = "";
      byId("series-episodes").disabled = false;
    }).catch(function (error) {
      byId("series-status").textContent =
        error && error.message ?
          error.message :
          "Falha ao carregar detalhes da série.";
      byId("series-episodes").disabled = true;
    });
  }

  function openSeriesEpisodes() {
    var parsed;
    var providerLabel;

    if (!seriesEntry || !seriesPayload) {
      return;
    }

    try {
      if (seriesEntry.kind === "pluto-series") {
        parsed = global.BlazzingPluto.buildEpisodeCatalog(
          seriesPayload,
          seriesEntry
        );
        providerLabel = "Pluto TV • Série";
      } else {
        if (!xtreamSession) {
          return;
        }

        parsed = global.BlazzingXtream.buildEpisodeCatalog(
          seriesPayload,
          xtreamSession
        );
        providerLabel = "Xtream • Série";
      }
    } catch (error) {
      byId("series-status").textContent =
        error && error.message ?
          error.message :
          "Falha ao montar os episódios.";
      return;
    }

    if (!parsed.items.length) {
      byId("series-status").textContent =
        "Nenhum episódio disponível.";
      return;
    }

    pushCatalogState();
    byId("catalog-provider").textContent = providerLabel;
    renderCatalog(parsed, seriesEntry.title);
  }

  function returnToCatalog() {
    releaseDetailArtwork();

    if (catalog) {
      showScreen("catalog");
      renderCatalogGroup(selectedGroup, true);
    } else {
      showHome();
    }
  }

  function isFavorite(item) {
    return !!(item && item.favoriteKey &&
      global.BlazzingStorage.isFavorite(item.favoriteKey));
  }

  function matchesSearch(item) {
    var query = String(catalogQuery || "").toLowerCase();

    if (!query) {
      return true;
    }

    return (
      String(item.title || "").toLowerCase() + " " +
      String(item.group || "").toLowerCase()
    ).indexOf(query) >= 0;
  }

  function matchingCatalogItems(group) {
    var matches = [];
    var i;
    var item;
    var groupMatches;

    if (catalog && catalog.lazyM3U) {
      return catalog.currentItems || [];
    }

    for (i = 0; i < catalog.items.length; i += 1) {
      item = catalog.items[i];
      groupMatches = group === FAVORITES_GROUP ?
        isFavorite(item) :
        (!group || item.group === group);

      if (groupMatches && matchesSearch(item)) {
        matches.push(item);
      }
    }

    return matches;
  }

  function remoteFavoriteIds() {
    var prefix;
    var keys;
    var ids = [];
    var i;

    if (!catalog || !catalog.lazyM3U || !catalog.sourceKey) {
      return ids;
    }

    prefix = "m3u:" + catalog.sourceKey + ":";
    keys = global.BlazzingStorage.favoriteKeys(prefix);

    for (i = 0; i < keys.length && i < 5000; i += 1) {
      ids.push(keys[i].slice(prefix.length));
    }

    return ids;
  }

  function renderArtwork(button, item) {
    var shell = button.querySelector(".media-artwork");
    var url = String(item && item.logo || "");
    var image;
    var initial;

    if (!shell) {
      return;
    }

    initial = String(item && item.title || "?").trim().charAt(0).toUpperCase();
    shell.textContent = initial || "?";

    if (!/^https?:\/\//i.test(url)) {
      return;
    }

    image = document.createElement("img");
    image.alt = "";
    image.setAttribute("loading", "lazy");
    image.setAttribute("referrerpolicy", "no-referrer");
    image.addEventListener("load", function () {
      shell.classList.add("has-image");
    });
    image.addEventListener("error", function () {
      global.BlazzingArtwork.invalidate(url);
      global.BlazzingArtwork.releaseImage(image);
      if (image.parentNode) {
        image.parentNode.removeChild(image);
      }
      shell.classList.remove("has-image");
    });
    shell.appendChild(image);
    global.BlazzingArtwork.apply(image, url);
  }

  function updateFavoriteBadge(button, item) {
    var badge = button.querySelector(".favorite-badge");
    var favorite = isFavorite(item);

    if (badge) {
      badge.textContent = favorite ? "★" : "";
    }

    if (favorite) {
      button.classList.add("is-favorite");
    } else {
      button.classList.remove("is-favorite");
    }

    button.setAttribute(
      "aria-label",
      item.title + (favorite ? ", favorito" : ", não favorito")
    );
  }

  function toggleFocusedFavorite() {
    var button = document.activeElement;
    var item;
    var favorite;

    if (!button || !button._blazzingEntry) {
      return false;
    }

    item = button._blazzingEntry;
    if (!item.favoriteKey) {
      return false;
    }

    favorite = global.BlazzingStorage.toggle(item.favoriteKey, {
      title: item.title,
      group: item.group,
      kind: item.kind
    });

    updateFavoriteBadge(button, item);

    if (catalog && catalog.lazyM3U) {
      if (selectedGroup === FAVORITES_GROUP && !favorite) {
        renderCatalogGroup(FAVORITES_GROUP);
      }
      return true;
    }

    if (selectedGroup === FAVORITES_GROUP && !favorite) {
      renderCatalogGroup(FAVORITES_GROUP, true);
    } else {
      byId("catalog-summary").textContent =
        matchingCatalogItems(selectedGroup).length +
        " item(ns) — " +
        global.BlazzingStorage.count() + " favorito(s) salvo(s)";
    }

    return true;
  }

  function selectCatalogGroupButton(group) {
    Array.prototype.forEach.call(
      document.querySelectorAll(".group-button"),
      function (node) {
        if (node.getAttribute("data-group") === group) {
          node.classList.add("is-selected");
        } else {
          node.classList.remove("is-selected");
        }
      }
    );
  }

  function focusCatalogLater() {
    var request = catalogFocusRequest;
    catalogFocusRequest = "";

    setTimeout(function () {
      var cards;

      if (request) {
        cards = document.querySelectorAll(".screen.is-active .media-card.focusable:not([disabled])");
        if (cards.length) {
          global.BlazzingNavigation.setFocus(
            request === "last" ? cards[cards.length - 1] : cards[0]
          );
          return;
        }
      }

      global.BlazzingNavigation.focusFirst();
    }, 0);
  }

  function mediaMetaText(item) {
    var parts = [item.group || "Sem categoria"];

    if ((item.kind === "live" || item.kind === "pluto-live") &&
        item.channelNumber) {
      parts.push("Canal " + item.channelNumber);
    }

    if (item.kind === "live" && item.archiveDays) {
      parts.push("Catch-up " + item.archiveDays + "d");
    }

    if (item.kind === "pluto-movie") {
      parts.push("Filme");
    } else if (item.kind === "pluto-series") {
      parts.push("Série");
    } else if (item.kind === "pluto-episode" && item.duration) {
      parts.push(item.duration);
    }

    return parts.join(" • ");
  }

  function renderCatalogCards(items) {
    var container = byId("catalog-items");
    var i;
    var item;
    var button;

    global.BlazzingArtwork.releaseTree(container);
    container.innerHTML = "";

    for (i = 0; i < items.length; i += 1) {
      item = items[i];
      button = document.createElement("button");
      button.type = "button";
      button.className = "media-card focusable";
      button.innerHTML =
        "<span class=\"media-artwork\" aria-hidden=\"true\"></span>" +
        "<span class=\"media-copy\"><strong></strong>" +
        "<span class=\"media-meta\"></span></span>" +
        "<span class=\"favorite-badge\" aria-hidden=\"true\"></span>";
      button.querySelector("strong").textContent = item.title;
      button.querySelector(".media-meta").textContent =
        mediaMetaText(item);
      button._blazzingEntry = item;
      renderArtwork(button, item);
      updateFavoriteBadge(button, item);
      button.addEventListener("click", (function (entry) {
        return function () {
          if (entry.kind === "series" || entry.kind === "pluto-series") {
            openSeriesDetails(entry);
          } else if (entry.kind === "vod" || entry.kind === "pluto-movie") {
            openVod(entry);
          } else if (entry.kind === "pluto-live") {
            playPluto(entry);
          } else if (entry.kind === "pluto-episode") {
            playPlutoVod(entry, "catalog");
          } else {
            playUrl(entry.url, entry.title, "catalog", entry);
          }
        };
      }(item)));
      container.appendChild(button);
    }
  }

  function remoteSessionExpired(error) {
    var message = error && error.message ? String(error.message) : "";

    return /session expired|does not exist/i.test(message);
  }

  function recoverRemoteCatalog(currentCatalog, group) {
    var restoreQuery = catalogQuery;
    var title = currentCatalog.sourceName || byId("catalog-title").textContent;

    if (!currentCatalog || currentCatalog.recovering || !currentCatalog.sourceUrl) {
      return;
    }

    currentCatalog.recovering = true;
    byId("catalog-summary").textContent =
      "Sessão da playlist expirou. Reindexando…";
    byId("catalog-prev").disabled = true;
    byId("catalog-next").disabled = true;

    global.BlazzingNetwork.fetchM3U(currentCatalog.sourceUrl)
      .then(function (result) {
        if (catalog !== currentCatalog) {
          if (result.mode === "paged-service" && result.sessionId) {
            global.BlazzingNetwork.releaseM3U(result.sessionId);
          }
          return;
        }

        if (result.mode !== "paged-service" || !result.sessionId) {
          throw new Error(
            "A playlist não pôde ser reaberta no serviço paginado webOS."
          );
        }

        currentCatalog.sessionId = result.sessionId;
        currentCatalog.groups = result.groups;
        currentCatalog.totalItems = result.itemCount;
        currentCatalog.totalBytes = result.totalBytes;
        currentCatalog.pageOffsets = [0];
        currentCatalog.currentItems = [];
        currentCatalog.hasMore = false;
        currentCatalog.lastGroup = null;
        currentCatalog.lastQuery = null;
        currentCatalog.recovering = false;
        catalogPage = 0;

        renderCatalog(currentCatalog, title, {
          group: group,
          page: 0,
          query: restoreQuery
        });
      })
      .catch(function (error) {
        if (catalog !== currentCatalog) {
          return;
        }

        currentCatalog.recovering = false;
        byId("catalog-summary").textContent =
          error && error.message ?
            error.message :
            "Falha ao reabrir a playlist.";
        byId("catalog-prev").disabled = catalogPage <= 0;
        byId("catalog-next").disabled = true;
      });
  }

  function renderRemoteCatalogGroup(group, preservePage) {
    var currentCatalog = catalog;
    var filterChanged;
    var startOffset;
    var favoriteIds;
    var requestId;

    selectedGroup = group;
    filterChanged =
      currentCatalog.lastGroup !== group ||
      currentCatalog.lastQuery !== catalogQuery;

    if (!preservePage || filterChanged) {
      catalogPage = 0;
      currentCatalog.pageOffsets = [0];
    }

    if (!currentCatalog.pageOffsets) {
      currentCatalog.pageOffsets = [0];
    }

    startOffset = Number(currentCatalog.pageOffsets[catalogPage] || 0);
    favoriteIds = group === FAVORITES_GROUP ? remoteFavoriteIds() : [];
    requestId = ++catalogRequestId;

    byId("catalog-summary").textContent = "Carregando página…";
    byId("catalog-prev").disabled = true;
    byId("catalog-next").disabled = true;
    selectCatalogGroupButton(group);

    global.BlazzingNetwork.queryM3U(currentCatalog.sessionId, {
      startOffset: startOffset,
      limit: catalogPageSize,
      group: group === FAVORITES_GROUP ? "" : group,
      query: catalogQuery,
      favoritesOnly: group === FAVORITES_GROUP,
      favoriteIds: favoriteIds
    }).then(function (result) {
      var i;
      var prefix;

      if (requestId !== catalogRequestId || catalog !== currentCatalog) {
        return;
      }

      if (!result.items.length && catalogPage > 0) {
        catalogPage -= 1;
        renderRemoteCatalogGroup(group, true);
        return;
      }

      prefix = "m3u:" + currentCatalog.sourceKey + ":";
      for (i = 0; i < result.items.length; i += 1) {
        result.items[i].favoriteKey = prefix + result.items[i].itemId;
      }

      currentCatalog.currentItems = result.items;
      currentCatalog.hasMore = result.hasMore;
      currentCatalog.lastGroup = group;
      currentCatalog.lastQuery = catalogQuery;

      if (result.hasMore) {
        currentCatalog.pageOffsets[catalogPage + 1] = result.nextOffset;
      }

      renderCatalogCards(result.items);

      if (!group && !catalogQuery) {
        byId("catalog-summary").textContent =
          currentCatalog.totalItems + " item(ns) — página " +
          (catalogPage + 1);
      } else {
        byId("catalog-summary").textContent =
          result.items.length + " item(ns) nesta página — página " +
          (catalogPage + 1);
      }

      byId("catalog-prev").disabled = catalogPage <= 0;
      byId("catalog-next").disabled = !result.hasMore;
      focusCatalogLater();
    }).catch(function (error) {
      if (requestId !== catalogRequestId || catalog !== currentCatalog) {
        return;
      }

      if (remoteSessionExpired(error)) {
        recoverRemoteCatalog(currentCatalog, group);
        return;
      }

      byId("catalog-summary").textContent =
        error && error.message ?
          error.message :
          "Falha ao consultar a playlist.";
      byId("catalog-prev").disabled = catalogPage <= 0;
      byId("catalog-next").disabled = true;
    });
  }

  function renderCatalogGroup(group, preservePage) {
    var matches;
    var totalPages;
    var start;
    var end;

    if (catalog && catalog.lazyM3U) {
      renderRemoteCatalogGroup(group, preservePage);
      return;
    }

    selectedGroup = group;
    if (!preservePage) {
      catalogPage = 0;
    }

    matches = matchingCatalogItems(group);
    totalPages = Math.max(1, Math.ceil(matches.length / catalogPageSize));

    if (catalogPage >= totalPages) {
      catalogPage = totalPages - 1;
    }
    if (catalogPage < 0) {
      catalogPage = 0;
    }

    start = catalogPage * catalogPageSize;
    end = Math.min(matches.length, start + catalogPageSize);

    renderCatalogCards(matches.slice(start, end));

    byId("catalog-summary").textContent =
      matches.length + " item(ns) — página " +
      (catalogPage + 1) + " de " + totalPages;

    byId("catalog-prev").disabled = catalogPage <= 0;
    byId("catalog-next").disabled = catalogPage >= totalPages - 1;
    selectCatalogGroupButton(group);
    focusCatalogLater();
  }

  function renderCatalog(parsed, name, restoreState) {
    var groups = byId("catalog-groups");
    var allButton;
    var i;
    var button;

    catalog = parsed;
    catalogQuery = restoreState ? String(restoreState.query || "") : "";
    byId("catalog-search").value = catalogQuery;
    byId("catalog-title").textContent = name || "Playlist";
    groups.innerHTML = "";

    allButton = document.createElement("button");
    allButton.type = "button";
    allButton.className = "group-button focusable";
    allButton.setAttribute("data-group", "");
    allButton.textContent = "Todos";
    allButton.addEventListener("click", function () {
      renderCatalogGroup("");
    });
    groups.appendChild(allButton);

    var favoritesButton = document.createElement("button");
    favoritesButton.type = "button";
    favoritesButton.className = "group-button focusable";
    favoritesButton.setAttribute("data-group", FAVORITES_GROUP);
    favoritesButton.textContent = "★ Favoritos";
    favoritesButton.addEventListener("click", function () {
      renderCatalogGroup(FAVORITES_GROUP);
    });
    groups.appendChild(favoritesButton);

    for (i = 0; i < parsed.groups.length; i += 1) {
      button = document.createElement("button");
      button.type = "button";
      button.className = "group-button focusable";
      button.setAttribute("data-group", parsed.groups[i]);
      button.textContent = parsed.groups[i];
      button.addEventListener("click", (function (groupName) {
        return function () {
          renderCatalogGroup(groupName);
        };
      }(parsed.groups[i])));
      groups.appendChild(button);
    }

    showScreen("catalog");

    if (restoreState) {
      catalogPage = Number(restoreState.page || 0);
      renderCatalogGroup(restoreState.group || "", true);
    } else {
      renderCatalogGroup("");
    }
  }

  function pushCatalogState() {
    if (!catalog) {
      return;
    }

    catalogStack.push({
      catalog: catalog,
      title: byId("catalog-title").textContent,
      provider: byId("catalog-provider").textContent,
      group: selectedGroup,
      page: catalogPage,
      query: catalogQuery
    });
  }

  function backFromCatalog() {
    var previous;

    if (!catalogStack.length) {
      showHome();
      return;
    }

    previous = catalogStack.pop();
    byId("catalog-provider").textContent = previous.provider;
    renderCatalog(previous.catalog, previous.title, {
      group: previous.group,
      page: previous.page,
      query: previous.query
    });
  }

  function loadPlaylist(url, name) {
    if (!validHttpUrl(url)) {
      byId("m3u-status").textContent = "Informe uma URL HTTP/HTTPS válida.";
      showScreen("m3u");
      return Promise.resolve();
    }

    byId("m3u-status").textContent =
      "Baixando e indexando playlist (até 128 MiB)…";
    showScreen("m3u");

    return global.BlazzingNetwork.fetchM3U(url).then(function (result) {
      var parsed;
      var sourceKey;
      var itemIndex;

      sourceKey = global.BlazzingStorage.fingerprint(
        result.finalUrl || url
      );

      if (result.mode === "paged-service") {
        if (!result.itemCount) {
          global.BlazzingNetwork.releaseM3U(result.sessionId);
          throw new Error("A playlist não contém itens reproduzíveis.");
        }

        releaseRemoteCatalog();
        parsed = {
          items: [],
          groups: result.groups,
          lazyM3U: true,
          sessionId: result.sessionId,
          sourceUrl: url,
          sourceName: name || "Playlist M3U",
          sourceKey: sourceKey,
          totalItems: result.itemCount,
          totalBytes: result.totalBytes,
          pageOffsets: [0],
          currentItems: [],
          hasMore: false,
          recovering: false,
          lastGroup: null,
          lastQuery: null
        };
      } else {
        byId("m3u-status").textContent = "Processando playlist…";
        parsed = global.BlazzingM3U.parse(
          result.text,
          result.finalUrl || url
        );

        for (itemIndex = 0; itemIndex < parsed.items.length; itemIndex += 1) {
          parsed.items[itemIndex].favoriteKey =
            "m3u:" + sourceKey + ":" +
            global.BlazzingStorage.fingerprint(
              parsed.items[itemIndex].url + "|" +
              parsed.items[itemIndex].title
            );
        }

        if (!parsed.items.length) {
          throw new Error("A playlist não contém itens reproduzíveis.");
        }
      }

      catalogStack = [];
      byId("catalog-provider").textContent =
        result.mode === "paged-service" ?
          "M3U • paginado" :
          "M3U";
      renderCatalog(parsed, name || "Playlist M3U");
    }).catch(function (error) {
      byId("m3u-status").textContent =
        error && error.message ? error.message : "Falha ao carregar a playlist.";
    });
  }


  function playPluto(entry) {
    if (!entry || !entry.plutoChannelId) {
      return;
    }

    byId("catalog-summary").textContent =
      "Preparando stream Pluto TV…";

    global.BlazzingNetwork.plutoStream(entry.plutoChannelId)
      .then(function (url) {
        playUrl(url, entry.title, "catalog", entry);
      })
      .catch(function (error) {
        byId("catalog-summary").textContent =
          error && error.message ?
            error.message :
            "Falha ao abrir canal Pluto TV.";
      });
  }

  function playPlutoVod(entry, returnScreen) {
    var statusNode;

    if (!entry || !entry.plutoVodPath) {
      return;
    }

    statusNode = returnScreen === "vod" ?
      byId("vod-status") :
      byId("catalog-summary");

    statusNode.textContent = "Preparando stream Pluto TV…";

    global.BlazzingNetwork.plutoVodStream(entry.plutoVodPath)
      .then(function (url) {
        statusNode.textContent = "";
        playUrl(
          url,
          entry.title,
          returnScreen || "catalog",
          entry
        );
      })
      .catch(function (error) {
        statusNode.textContent =
          error && error.message ?
            error.message :
            "Falha ao abrir conteúdo Pluto TV.";
      });
  }

  function loadPlutoCatalog() {
    byId("pluto-status").textContent =
      "Criando sessão e carregando canais…";
    showScreen("pluto");

    global.BlazzingNetwork.plutoLive()
      .then(function (payload) {
        var parsed = global.BlazzingPluto.buildLiveCatalog(payload);

        if (!parsed.items.length) {
          throw new Error("Pluto TV não retornou canais para esta região.");
        }

        catalogStack = [];
        xtreamSession = null;
        byId("catalog-provider").textContent = "Pluto TV • Live";
        renderCatalog(parsed, "Pluto TV");
      })
      .catch(function (error) {
        byId("pluto-status").textContent =
          error && error.message ?
            error.message :
            "Falha ao carregar Pluto TV.";
      });
  }


  function loadPlutoVodCatalog() {
    byId("pluto-status").textContent =
      "Carregando filmes e séries…";
    showScreen("pluto");

    global.BlazzingNetwork.plutoVod()
      .then(function (payload) {
        var parsed = global.BlazzingPluto.buildVodCatalog(payload);

        if (!parsed.items.length) {
          throw new Error(
            "Pluto TV não retornou filmes ou séries para esta região."
          );
        }

        catalogStack = [];
        xtreamSession = null;
        byId("catalog-provider").textContent = "Pluto TV • VOD";
        renderCatalog(parsed, "Filmes e séries");
      })
      .catch(function (error) {
        byId("pluto-status").textContent =
          error && error.message ?
            error.message :
            "Falha ao carregar Pluto VOD.";
      });
  }

  function startPairing() {
    byId("pair-status").textContent = "Criando sessão segura…";
    byId("pair-url").textContent = "—";
    byId("pair-qr").innerHTML = "";
    showScreen("pair");

    stopPairing().then(function () {
      return global.BlazzingPairing.start({
        onStatus: function (message) {
          byId("pair-status").textContent = message;
        },
        onPayload: function (payload) {
          pairingSession = null;

          if (!payload || !validHttpUrl(payload.url || "")) {
            byId("pair-status").textContent = "O celular enviou uma URL inválida.";
            return;
          }

          loadPlaylist(payload.url, payload.name || "Playlist");
        },
        onExpired: function () {
          pairingSession = null;
        },
        onError: function () {
          pairingSession = null;
        }
      });
    }).then(function (session) {
      pairingSession = session;
      byId("pair-url").textContent = session.pageUrl;
      global.BlazzingQR.render(byId("pair-qr"), session.pageUrl);
    }).catch(function (error) {
      byId("pair-status").textContent = error.message || "Falha ao iniciar pareamento.";
    });
  }

  byId("action-pair").addEventListener("click", startPairing);

  byId("action-m3u").addEventListener("click", function () {
    byId("m3u-status").textContent = "";
    showScreen("m3u");
  });

  byId("action-xtream").addEventListener("click", function () {
    byId("xtream-status").textContent = "";
    byId("xtream-password").value = "";
    restoreXtreamProfile();
    showScreen("xtream");
  });

  byId("action-pluto").addEventListener("click", function () {
    byId("pluto-status").textContent = "";
    showScreen("pluto");
  });

  byId("pluto-live").addEventListener("click", loadPlutoCatalog);
  byId("pluto-vod").addEventListener("click", loadPlutoVodCatalog);

  byId("action-manual").addEventListener("click", function () {
    byId("manual-status").textContent = "";
    showScreen("manual");
  });

  byId("action-about").addEventListener("click", function () {
    showScreen("about");
  });

  byId("m3u-load").addEventListener("click", function () {
    loadPlaylist(byId("m3u-url").value.trim(), "Playlist M3U");
  });

  function loadXtreamCatalog(kind) {
    var creds;
    var categoryAction;
    var streamAction;
    var loadingLabel;

    try {
      creds = global.BlazzingXtream.credentials(
        byId("xtream-server").value,
        byId("xtream-username").value,
        byId("xtream-password").value
      );
    } catch (error) {
      byId("xtream-status").textContent = error.message;
      return;
    }

    if (kind === "vod") {
      categoryAction = "get_vod_categories";
      streamAction = "get_vod_streams";
      loadingLabel = "filmes";
    } else if (kind === "series") {
      categoryAction = "get_series_categories";
      streamAction = "get_series";
      loadingLabel = "séries";
    } else {
      categoryAction = "get_live_categories";
      streamAction = "get_live_streams";
      loadingLabel = "canais";
    }

    byId("xtream-status").textContent = "Autenticando…";

    global.BlazzingNetwork.xtreamRequest(creds, "").then(function (auth) {
      if (!global.BlazzingXtream.authAccepted(auth)) {
        throw new Error("Provider rejeitou as credenciais.");
      }

      if (byId("xtream-remember").checked) {
        global.BlazzingStorage.setXtreamProfile(
          creds.server,
          creds.username
        );
      } else {
        global.BlazzingStorage.clearXtreamProfile();
      }

      byId("xtream-status").textContent = "Carregando " + loadingLabel + "…";
      return Promise.all([
        global.BlazzingNetwork.xtreamRequest(creds, categoryAction),
        global.BlazzingNetwork.xtreamRequest(creds, streamAction)
      ]);
    }).then(function (responses) {
      var parsed;

      if (kind === "vod") {
        parsed = global.BlazzingXtream.buildVodCatalog(
          responses[0],
          responses[1],
          creds
        );
      } else if (kind === "series") {
        parsed = global.BlazzingXtream.buildSeriesCatalog(
          responses[0],
          responses[1],
          creds
        );
      } else {
        parsed = global.BlazzingXtream.buildLiveCatalog(
          responses[0],
          responses[1],
          creds
        );
      }

      xtreamSession = creds;
      byId("xtream-password").value = "";

      if (!parsed.items.length) {
        throw new Error(
          kind === "vod" ?
            "Provider não retornou filmes." :
            (kind === "series" ?
              "Provider não retornou séries." :
              "Provider não retornou canais live.")
        );
      }

      catalogStack = [];
      byId("catalog-provider").textContent =
        kind === "vod" ? "Xtream • Filmes" :
          (kind === "series" ? "Xtream • Séries" : "Xtream • TV");
      renderCatalog(
        parsed,
        kind === "vod" ? "Filmes" :
          (kind === "series" ? "Séries" : "TV ao vivo")
      );
    }).catch(function (error) {
      byId("xtream-password").value = "";
      byId("xtream-status").textContent =
        error && error.message ? error.message : "Falha ao carregar Xtream.";
    });
  }

  byId("xtream-live").addEventListener("click", function () {
    loadXtreamCatalog("live");
  });

  byId("xtream-vod").addEventListener("click", function () {
    loadXtreamCatalog("vod");
  });

  byId("xtream-series").addEventListener("click", function () {
    loadXtreamCatalog("series");
  });

  byId("xtream-remember").addEventListener("change", function () {
    if (!byId("xtream-remember").checked) {
      global.BlazzingStorage.clearXtreamProfile();
    }
  });

  byId("manual-play").addEventListener("click", function () {
    var url = byId("manual-url").value.trim();
    if (!validHttpUrl(url)) {
      byId("manual-status").textContent = "Informe uma URL HTTP/HTTPS válida.";
      return;
    }
    playUrl(url, "Stream manual", "home");
  });

  byId("m3u-back").addEventListener("click", showHome);
  byId("xtream-back").addEventListener("click", showHome);
  byId("pluto-back").addEventListener("click", showHome);
  byId("manual-back").addEventListener("click", showHome);
  byId("about-back").addEventListener("click", showHome);
  byId("pair-cancel").addEventListener("click", showHome);
  byId("vod-play").addEventListener("click", function () {
    if (!vodEntry) {
      return;
    }

    if (vodEntry.kind === "pluto-movie") {
      playPlutoVod(vodEntry, "vod");
    } else {
      playUrl(vodEntry.url, vodEntry.title, "vod", vodEntry);
    }
  });

  byId("vod-back").addEventListener("click", returnToCatalog);
  byId("series-back").addEventListener("click", returnToCatalog);
  byId("series-episodes").addEventListener("click", openSeriesEpisodes);

  byId("catalog-search-apply").addEventListener("click", function () {
    catalogQuery = byId("catalog-search").value.trim().toLowerCase();
    renderCatalogGroup(selectedGroup);
  });

  byId("catalog-search-clear").addEventListener("click", function () {
    byId("catalog-search").value = "";
    catalogQuery = "";
    renderCatalogGroup(selectedGroup);
  });

  byId("catalog-prev").addEventListener("click", function () {
    if (catalogPage > 0) {
      catalogPage -= 1;
      renderCatalogGroup(selectedGroup, true);
    }
  });

  byId("catalog-next").addEventListener("click", function () {
    var matches;
    var totalPages;

    if (catalog && catalog.lazyM3U) {
      if (catalog.hasMore) {
        catalogPage += 1;
        renderCatalogGroup(selectedGroup, true);
      }
      return;
    }

    matches = matchingCatalogItems(selectedGroup);
    totalPages = Math.max(1, Math.ceil(matches.length / catalogPageSize));

    if (catalogPage < totalPages - 1) {
      catalogPage += 1;
      renderCatalogGroup(selectedGroup, true);
    }
  });

  byId("catalog-back").addEventListener("click", backFromCatalog);

  document.addEventListener("blazzing-navigation-boundary", function (event) {
    var detail = event && event.detail ? event.detail : {};
    var origin = detail.origin;
    var matches;
    var totalPages;

    if (activeScreen !== "catalog" ||
        !origin ||
        !origin.classList ||
        !origin.classList.contains("media-card")) {
      return;
    }

    if (Number(detail.dy || 0) > 0) {
      if (catalog && catalog.lazyM3U) {
        if (!catalog.hasMore) {
          return;
        }

        catalogPage += 1;
        catalogFocusRequest = "first";
        renderCatalogGroup(selectedGroup, true);
        return;
      }

      matches = matchingCatalogItems(selectedGroup);
      totalPages = Math.max(1, Math.ceil(matches.length / catalogPageSize));
      if (catalogPage < totalPages - 1) {
        catalogPage += 1;
        catalogFocusRequest = "first";
        renderCatalogGroup(selectedGroup, true);
      }
      return;
    }

    if (Number(detail.dy || 0) < 0 && catalogPage > 0) {
      catalogPage -= 1;
      catalogFocusRequest = "last";
      renderCatalogGroup(selectedGroup, true);
    }
  });


  byId("player-retry").addEventListener("click", function () {
    if (!activePlayback) {
      return;
    }

    savePlayerProgress();
    global.BlazzingPlayer.stop();
    activePlayback.sourceIndex = 0;
    activePlayback.resumeAt = currentResumePoint(activePlayback);
    byId("player-status").textContent = "Tentando novamente…";
    setRetryVisible(false);
    openPlaybackCandidate(activePlayback);
  });

  byId("player-back").addEventListener("click", function () {
    savePlayerProgress();
    global.BlazzingPlayer.stop();
    activeProgressKey = "";
    activePlayback = null;
    setRetryVisible(false);

    if (playerReturnScreen === "vod" && vodEntry) {
      showScreen("vod");
    } else if (playerReturnScreen === "catalog" && catalog) {
      showScreen("catalog");
      renderCatalogGroup(selectedGroup, true);
    } else {
      showHome();
    }
  });

  document.addEventListener("blazzing-back", function () {
    if (activeScreen === "home") {
      return;
    }

    if (activeScreen === "player" && playerReturnScreen === "vod" && vodEntry) {
      savePlayerProgress();
      global.BlazzingPlayer.stop();
      activeProgressKey = "";
      activePlayback = null;
      setRetryVisible(false);
      showScreen("vod");
      return;
    }

    if (activeScreen === "player" && playerReturnScreen === "catalog" && catalog) {
      savePlayerProgress();
      global.BlazzingPlayer.stop();
      activeProgressKey = "";
      activePlayback = null;
      setRetryVisible(false);
      showScreen("catalog");
      renderCatalogGroup(selectedGroup, true);
      return;
    }

    if (activeScreen === "vod" || activeScreen === "series") {
      returnToCatalog();
      return;
    }

    if (activeScreen === "catalog") {
      backFromCatalog();
      return;
    }

    showHome();
  });

  document.addEventListener("keydown", function (event) {
    if (activeScreen === "catalog" &&
        event.keyCode === 13 &&
        event.target && event.target.id === "catalog-search") {
      catalogQuery = byId("catalog-search").value.trim().toLowerCase();
      renderCatalogGroup(selectedGroup);
      event.preventDefault();
      return;
    }

    if (activeScreen === "catalog" &&
        (event.keyCode === 405 || event.keyCode === 70)) {
      if (toggleFocusedFavorite()) {
        event.preventDefault();
      }
      return;
    }

    if (activeScreen === "player" &&
        event.keyCode === 13 &&
        (!event.target || event.target.tagName !== "BUTTON")) {
      global.BlazzingPlayer.togglePause();
    }
  });

  restoreXtreamProfile();
  showScreen("home");
}(window));
