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
    var options = callbacks || {};
    var resumeAt = Number(options.resumeAt || 0);
    var lastProgressSecond = -1;
    var errorReported = false;

    function reportError(message) {
      if (errorReported) {
        return;
      }
      errorReported = true;

      if (callbacks && callbacks.onError) {
        callbacks.onError(message);
      }
    }

    element.onerror = function () {
      reportError("A TV não conseguiu reproduzir este stream.");
    };

    element.onloadedmetadata = function () {
      var duration = Number(element.duration);

      if (resumeAt >= 10 &&
          (!isFinite(duration) || duration <= 0 || resumeAt < duration - 30)) {
        try {
          element.currentTime = resumeAt;
          lastProgressSecond = Math.floor(resumeAt);
          if (options.onResume) {
            options.onResume(resumeAt);
          }
        } catch (error) {
          // Some streams reject seeks until playback has advanced.
        }
      }
    };

    element.ontimeupdate = function () {
      var current = Math.floor(Number(element.currentTime || 0));

      if (!options.onProgress || current < 10) {
        return;
      }

      if (lastProgressSecond < 0 || Math.abs(current - lastProgressSecond) >= 15) {
        lastProgressSecond = current;
        options.onProgress(current, Number(element.duration || 0));
      }
    };

    element.onended = function () {
      if (options.onEnded) {
        options.onEnded();
      }
    };

    element.onplaying = function () {
      if (options.onPlaying) {
        options.onPlaying();
      }
    };

    element.src = url;
    element.load();

    try {
      var promise = element.play();
      if (promise && promise.catch) {
        promise.catch(function () {
          reportError("A reprodução foi bloqueada ou o stream é incompatível.");
        });
      }
    } catch (error) {
      reportError(error.message || "Falha ao iniciar reprodução.");
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

  function position() {
    var element = ensureVideo();

    return {
      currentTime: Number(element.currentTime || 0),
      duration: Number(element.duration || 0)
    };
  }

  function stop() {
    var element = ensureVideo();
    element.pause();
    element.onerror = null;
    element.onloadedmetadata = null;
    element.ontimeupdate = null;
    element.onended = null;
    element.onplaying = null;
    element.removeAttribute("src");
    element.load();
  }

  global.BlazzingPlayer = {
    open: open,
    stop: stop,
    togglePause: togglePause,
    position: position
  };
}(window));
