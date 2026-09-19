(function (global) {
  "use strict";

  var SERVICE_URI = "luna://io.github.xoykor.blazzing.network";
  var MAX_BROWSER_API_BYTES = 8 * 1024 * 1024;
  var MAX_BROWSER_M3U_BYTES = 128 * 1024 * 1024;

  function serviceAvailable() {
    return !!(
      global.webOS &&
      global.webOS.service &&
      typeof global.webOS.service.request === "function"
    );
  }

  function serviceRequest(method, parameters) {
    return new Promise(function (resolve, reject) {
      var request;

      request = global.webOS.service.request(SERVICE_URI, {
        method: method,
        parameters: parameters,
        onSuccess: function (response) {
          request = null;
          if (!response || response.returnValue === false) {
            reject(new Error(response && response.errorText ?
              response.errorText : "Falha no serviço de rede webOS."));
            return;
          }
          resolve(response);
        },
        onFailure: function (error) {
          request = null;
          reject(new Error(error && error.errorText ?
            error.errorText : "Falha no serviço de rede webOS."));
        }
      });
    });
  }

  function fetchInBrowser(url, maxBytes, tooLargeMessage) {
    var limit = Number(maxBytes || MAX_BROWSER_API_BYTES);
    var limitMessage = tooLargeMessage ||
      "Resposta grande demais para o modo direto do Simulator.";

    return fetch(url, {
      method: "GET",
      cache: "no-store",
      credentials: "omit"
    }).then(function (response) {
      var contentLength = Number(response.headers.get("content-length") || 0);

      if (!response.ok) {
        throw new Error("Servidor respondeu HTTP " + response.status + ".");
      }

      if (contentLength > limit) {
        throw new Error(limitMessage);
      }

      return response.text();
    }).then(function (text) {
      if (text.length > limit) {
        throw new Error(limitMessage);
      }
      return text;
    }).catch(function (error) {
      throw new Error(
        "Não foi possível consultar o provider. No Simulator isso pode ser CORS. " +
        (error && error.message ? error.message : "")
      );
    });
  }

  function browserM3U(url) {
    return fetchInBrowser(
      url,
      MAX_BROWSER_M3U_BYTES,
      "Playlist excede o limite de 128 MiB."
    ).then(function (text) {
      return {
        mode: "memory",
        text: text,
        finalUrl: url,
        totalBytes: text.length,
        transport: "browser"
      };
    });
  }

  function fetchM3U(url) {
    if (!serviceAvailable()) {
      return browserM3U(url);
    }

    return serviceRequest("prepareM3U", {
      url: url
    }).then(function (response) {
      if (!response.sessionId ||
          !Array.isArray(response.groups) ||
          typeof response.itemCount !== "number") {
        throw new Error("O serviço webOS retornou uma sessão M3U inválida.");
      }

      return {
        mode: "paged-service",
        sessionId: response.sessionId,
        finalUrl: response.finalUrl || url,
        totalBytes: Number(response.totalBytes || 0),
        itemCount: Number(response.itemCount || 0),
        groups: response.groups,
        maxBytes: Number(response.maxBytes || MAX_BROWSER_M3U_BYTES),
        transport: "service"
      };
    }).catch(function (error) {
      if (error && /128 MiB/i.test(error.message || "")) {
        throw error;
      }

      return browserM3U(url).then(function (result) {
        result.transport = "browser-fallback";
        return result;
      });
    });
  }

  function queryM3U(sessionId, options) {
    var opts = options || {};

    if (!serviceAvailable()) {
      return Promise.reject(new Error(
        "Paginação M3U requer o serviço webOS empacotado."
      ));
    }

    return serviceRequest("queryM3U", {
      sessionId: sessionId,
      startOffset: Number(opts.startOffset || 0),
      limit: Number(opts.limit || 48),
      group: String(opts.group || ""),
      query: String(opts.query || ""),
      favoritesOnly: opts.favoritesOnly === true,
      favoriteIds: Array.isArray(opts.favoriteIds) ?
        opts.favoriteIds.slice(0, 5000) : []
    }).then(function (response) {
      return {
        items: Array.isArray(response.items) ? response.items : [],
        hasMore: response.hasMore === true,
        nextOffset: Number(response.nextOffset || 0)
      };
    });
  }

  function releaseM3U(sessionId) {
    if (!sessionId || !serviceAvailable()) {
      return Promise.resolve();
    }

    return serviceRequest("releaseM3U", {
      sessionId: sessionId
    }).catch(function () {
      // Temporary sessions also expire server-side, so cleanup is best effort.
    });
  }

  function xtreamRequest(creds, action, params) {
    if (serviceAvailable()) {
      return serviceRequest("xtreamRequest", {
        server: creds.server,
        username: creds.username,
        password: creds.password,
        action: action || "",
        seriesId: params && params.seriesId != null ? String(params.seriesId) : "",
        vodId: params && params.vodId != null ? String(params.vodId) : ""
      }).then(function (response) {
        if (typeof response.text !== "string") {
          throw new Error("O serviço webOS não retornou JSON Xtream.");
        }
        try {
          return JSON.parse(response.text);
        } catch (error) {
          throw new Error("Provider Xtream retornou JSON inválido.");
        }
      }).catch(function () {
        return fetchInBrowser(global.BlazzingXtream.apiUrl(creds, action, params)).then(function (text) {
          try {
            return JSON.parse(text);
          } catch (error) {
            throw new Error("Provider Xtream retornou JSON inválido.");
          }
        });
      });
    }

    return fetchInBrowser(global.BlazzingXtream.apiUrl(creds, action, params)).then(function (text) {
      try {
        return JSON.parse(text);
      } catch (error) {
        throw new Error("Provider Xtream retornou JSON inválido.");
      }
    });
  }

  global.BlazzingNetwork = {
    fetchM3U: fetchM3U,
    queryM3U: queryM3U,
    releaseM3U: releaseM3U,
    xtreamRequest: xtreamRequest
  };
}(window));
