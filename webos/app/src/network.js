(function (global) {
  "use strict";

  var SERVICE_URI = "luna://io.github.xoykor.blazzing.network";
  var MAX_BROWSER_BYTES = 8 * 1024 * 1024;

  function fetchThroughService(url) {
    return new Promise(function (resolve, reject) {
      var request;

      request = global.webOS.service.request(SERVICE_URI, {
        method: "fetchM3U",
        parameters: { url: url },
        onSuccess: function (response) {
          request = null;
          if (!response || response.returnValue === false || typeof response.text !== "string") {
            reject(new Error(response && response.errorText ?
              response.errorText : "O serviço webOS não retornou a playlist."));
            return;
          }
          resolve({
            text: response.text,
            finalUrl: response.finalUrl || url,
            transport: "service"
          });
        },
        onFailure: function (error) {
          request = null;
          reject(new Error(error && error.errorText ?
            error.errorText : "Falha no serviço de rede webOS."));
        }
      });
    });
  }

  function fetchInBrowser(url) {
    return fetch(url, {
      method: "GET",
      cache: "no-store",
      credentials: "omit"
    }).then(function (response) {
      if (!response.ok) {
        throw new Error("Servidor M3U respondeu HTTP " + response.status + ".");
      }
      return response.text();
    }).then(function (text) {
      if (text.length > MAX_BROWSER_BYTES) {
        throw new Error("Playlist grande demais para o modo direto do Simulator.");
      }
      return {
        text: text,
        finalUrl: url,
        transport: "browser"
      };
    }).catch(function (error) {
      throw new Error(
        "Não foi possível baixar a playlist. No Simulator isso pode ser CORS. " +
        (error && error.message ? error.message : "")
      );
    });
  }

  function fetchM3U(url) {
    if (global.webOS && global.webOS.service &&
        typeof global.webOS.service.request === "function") {
      return fetchThroughService(url);
    }
    return fetchInBrowser(url);
  }

  global.BlazzingNetwork = {
    fetchM3U: fetchM3U
  };
}(window));
