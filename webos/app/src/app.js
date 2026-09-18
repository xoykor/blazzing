(function (global) {
  "use strict";

  var activeScreen = "home";
  var pairingSession = null;

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

  function showHome() {
    stopPairing();
    global.BlazzingPlayer.stop();
    showScreen("home");
  }

  function validHttpUrl(value) {
    return /^https?:\/\//i.test(value);
  }

  function playUrl(url, title) {
    if (!validHttpUrl(url)) {
      byId("manual-status").textContent = "Informe uma URL HTTP/HTTPS válida.";
      return;
    }

    byId("player-title").textContent = title || "Stream";
    byId("player-status").textContent = "Carregando…";
    showScreen("player");

    global.BlazzingPlayer.open(url, {
      onPlaying: function () {
        byId("player-status").textContent = "Reproduzindo";
      },
      onError: function (message) {
        byId("player-status").textContent = message;
      }
    });
  }

  function startPairing() {
    byId("pair-status").textContent = "Criando sessão segura…";
    byId("pair-url").textContent = "—";
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

          playUrl(payload.url, payload.name || "Playlist");
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
    }).catch(function (error) {
      byId("pair-status").textContent = error.message || "Falha ao iniciar pareamento.";
    });
  }

  byId("action-pair").addEventListener("click", startPairing);

  byId("action-manual").addEventListener("click", function () {
    byId("manual-status").textContent = "";
    showScreen("manual");
  });

  byId("action-about").addEventListener("click", function () {
    showScreen("about");
  });

  byId("manual-play").addEventListener("click", function () {
    playUrl(byId("manual-url").value.trim(), "Stream manual");
  });

  byId("manual-back").addEventListener("click", showHome);
  byId("about-back").addEventListener("click", showHome);
  byId("pair-cancel").addEventListener("click", showHome);
  byId("player-back").addEventListener("click", showHome);

  document.addEventListener("blazzing-back", function () {
    if (activeScreen === "home") {
      return;
    }
    showHome();
  });

  document.addEventListener("keydown", function (event) {
    if (activeScreen === "player" && event.keyCode === 13) {
      global.BlazzingPlayer.togglePause();
    }
  });

  showScreen("home");
}(window));
