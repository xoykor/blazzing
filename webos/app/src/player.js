(function (global) {
  "use strict";

  var video = null;

  function ensureVideo() {
    if (!video) {
      video = document.getElementById("video");
    }
    return video;
  }

  function open(url, callbacks) {
    var element = ensureVideo();

    element.onerror = function () {
      if (callbacks && callbacks.onError) {
        callbacks.onError("A TV não conseguiu reproduzir este stream.");
      }
    };

    element.onplaying = function () {
      if (callbacks && callbacks.onPlaying) {
        callbacks.onPlaying();
      }
    };

    element.src = url;
    element.load();

    try {
      var promise = element.play();
      if (promise && promise.catch) {
        promise.catch(function () {
          if (callbacks && callbacks.onError) {
            callbacks.onError("A reprodução foi bloqueada ou o stream é incompatível.");
          }
        });
      }
    } catch (error) {
      if (callbacks && callbacks.onError) {
        callbacks.onError(error.message || "Falha ao iniciar reprodução.");
      }
    }
  }

  function togglePause() {
    var element = ensureVideo();
    if (element.paused) {
      element.play();
    } else {
      element.pause();
    }
  }

  function stop() {
    var element = ensureVideo();
    element.pause();
    element.removeAttribute("src");
    element.load();
  }

  global.BlazzingPlayer = {
    open: open,
    stop: stop,
    togglePause: togglePause
  };
}(window));
