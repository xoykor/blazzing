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
  var activeProgressKey = "";
  var vodEntry = null;
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

  function showHome() {
    stopPairing();
    savePlayerProgress();
    global.BlazzingPlayer.stop();
    activeProgressKey = "";
    vodEntry = null;
    catalogStack = [];
    xtreamSession = null;
    byId("xtream-password").value = "";
    showScreen("home");
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

  function playUrl(url, title, returnScreen, entry) {
    var resumeAt = 0;
    var trackProgress = entry &&
      (entry.kind === "vod" || entry.kind === "episode") &&
      entry.favoriteKey;

    if (!validHttpUrl(url)) {
      return;
    }

    activeProgressKey = trackProgress ? entry.favoriteKey : "";
    if (activeProgressKey) {
      resumeAt = global.BlazzingStorage.getProgress(activeProgressKey);
    }

    playerReturnScreen = returnScreen || "home";
    byId("player-title").textContent = title || "Stream";
    byId("player-status").textContent =
      resumeAt >= 10 ? "Retomando em " + formatTime(resumeAt) + "…" : "Carregando…";
    showScreen("player");

    global.BlazzingPlayer.open(url, {
      resumeAt: resumeAt,
      onResume: function (seconds) {
        byId("player-status").textContent =
          "Retomando em " + formatTime(seconds) + "…";
      },
      onPlaying: function () {
        byId("player-status").textContent =
          resumeAt >= 10 ?
            "Reproduzindo • retomado em " + formatTime(resumeAt) :
            "Reproduzindo";
      },
      onProgress: function (seconds, duration) {
        if (activeProgressKey) {
          global.BlazzingStorage.setProgress(
            activeProgressKey,
            seconds,
            duration
          );
        }
      },
      onEnded: function () {
        if (activeProgressKey) {
          global.BlazzingStorage.clearProgress(activeProgressKey);
          activeProgressKey = "";
        }
      },
      onError: function (message) {
        byId("player-status").textContent = message;
      }
    });
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
    image.removeAttribute("src");
    image.style.display = "none";

    if (/^https?:\/\//i.test(logo)) {
      image.onload = function () {
        image.style.display = "block";
      };
      image.onerror = function () {
        image.style.display = "none";
      };
      image.src = logo;
    }

    showScreen("vod");
  }

  function openVod(entry) {
    var fallback = global.BlazzingXtream.buildVodMetadata({}, entry);

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

  function returnToCatalog() {
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
      if (image.parentNode) {
        image.parentNode.removeChild(image);
      }
      shell.classList.remove("has-image");
    });
    image.src = url;
    shell.appendChild(image);
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

  function renderCatalogGroup(group, preservePage) {
    var container = byId("catalog-items");
    var matches;
    var totalPages;
    var start;
    var end;
    var i;
    var item;
    var button;

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
    container.innerHTML = "";

    for (i = start; i < end; i += 1) {
      item = matches[i];
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
        item.group || "Sem categoria";
      button._blazzingEntry = item;
      renderArtwork(button, item);
      updateFavoriteBadge(button, item);
      button.addEventListener("click", (function (entry) {
        return function () {
          if (entry.kind === "series") {
            openSeries(entry);
          } else if (entry.kind === "vod") {
            openVod(entry);
          } else {
            playUrl(entry.url, entry.title, "catalog", entry);
          }
        };
      }(item)));
      container.appendChild(button);
    }

    byId("catalog-summary").textContent =
      matches.length + " item(ns) — página " + (catalogPage + 1) + " de " + totalPages;

    byId("catalog-prev").disabled = catalogPage <= 0;
    byId("catalog-next").disabled = catalogPage >= totalPages - 1;

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

    setTimeout(function () {
      global.BlazzingNavigation.focusFirst();
    }, 0);
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

    byId("m3u-status").textContent = "Baixando playlist…";
    showScreen("m3u");

    return global.BlazzingNetwork.fetchM3U(url).then(function (result) {
      var parsed;

      byId("m3u-status").textContent = "Processando playlist…";
      parsed = global.BlazzingM3U.parse(result.text, result.finalUrl || url);

      var sourceKey = global.BlazzingStorage.fingerprint(result.finalUrl || url);
      var itemIndex;
      for (itemIndex = 0; itemIndex < parsed.items.length; itemIndex += 1) {
        parsed.items[itemIndex].favoriteKey =
          "m3u:" + sourceKey + ":" +
          global.BlazzingStorage.fingerprint(
            parsed.items[itemIndex].url + "|" + parsed.items[itemIndex].title
          );
      }

      if (!parsed.items.length) {
        throw new Error("A playlist não contém itens reproduzíveis.");
      }

      catalogStack = [];
      byId("catalog-provider").textContent = "M3U";
      renderCatalog(parsed, name || "Playlist M3U");
    }).catch(function (error) {
      byId("m3u-status").textContent =
        error && error.message ? error.message : "Falha ao carregar a playlist.";
    });
  }

  function openSeries(entry) {
    if (!xtreamSession || !entry || !entry.seriesId) {
      return;
    }

    byId("catalog-summary").textContent = "Carregando episódios…";

    global.BlazzingNetwork.xtreamRequest(
      xtreamSession,
      "get_series_info",
      { seriesId: entry.seriesId }
    ).then(function (payload) {
      var parsed = global.BlazzingXtream.buildEpisodeCatalog(
        payload,
        xtreamSession
      );

      pushCatalogState();
      byId("catalog-provider").textContent = "Xtream • Série";
      renderCatalog(parsed, entry.title);
    }).catch(function (error) {
      byId("catalog-summary").textContent =
        error && error.message ? error.message : "Falha ao carregar episódios.";
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
    showScreen("xtream");
  });

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
  byId("manual-back").addEventListener("click", showHome);
  byId("about-back").addEventListener("click", showHome);
  byId("pair-cancel").addEventListener("click", showHome);
  byId("vod-play").addEventListener("click", function () {
    if (vodEntry) {
      playUrl(vodEntry.url, vodEntry.title, "vod", vodEntry);
    }
  });

  byId("vod-back").addEventListener("click", returnToCatalog);

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
    var matches = matchingCatalogItems(selectedGroup);
    var totalPages = Math.max(1, Math.ceil(matches.length / catalogPageSize));

    if (catalogPage < totalPages - 1) {
      catalogPage += 1;
      renderCatalogGroup(selectedGroup, true);
    }
  });

  byId("catalog-back").addEventListener("click", backFromCatalog);
  byId("player-back").addEventListener("click", function () {
    savePlayerProgress();
    global.BlazzingPlayer.stop();
    activeProgressKey = "";
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
      showScreen("vod");
      return;
    }

    if (activeScreen === "player" && playerReturnScreen === "catalog" && catalog) {
      savePlayerProgress();
      global.BlazzingPlayer.stop();
      activeProgressKey = "";
      showScreen("catalog");
      renderCatalogGroup(selectedGroup, true);
      return;
    }

    if (activeScreen === "vod") {
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

    if (activeScreen === "player" && event.keyCode === 13) {
      global.BlazzingPlayer.togglePause();
    }
  });

  showScreen("home");
}(window));
