(function (global) {
  "use strict";

  var SERVICE_URI = "luna://io.github.xoykor.blazzing.network";
  var MAX_BROWSER_BYTES = 8 * 1024 * 1024;

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

  function fetchInBrowser(url) {
    return fetch(url, {
      method: "GET",
      cache: "no-store",
      credentials: "omit"
    }).then(function (response) {
      if (!response.ok) {
        throw new Error("Servidor respondeu HTTP " + response.status + ".");
      }
      return response.text();
    }).then(function (text) {
      if (text.length > MAX_BROWSER_BYTES) {
        throw new Error("Resposta grande demais para o modo direto do Simulator.");
      }
      return text;
    }).catch(function (error) {
      throw new Error(
        "Não foi possível consultar o provider. No Simulator isso pode ser CORS. " +
        (error && error.message ? error.message : "")
      );
    });
  }

  function fetchM3U(url) {
    if (global.webOS && global.webOS.service &&
        typeof global.webOS.service.request === "function") {
      return serviceRequest("fetchM3U", { url: url }).then(function (response) {
        if (typeof response.text !== "string") {
          throw new Error("O serviço webOS não retornou a playlist.");
        }
        return {
          text: response.text,
          finalUrl: response.finalUrl || url,
          transport: "service"
        };
      });
    }

    return fetchInBrowser(url).then(function (text) {
      return {
        text: text,
        finalUrl: url,
        transport: "browser"
      };
    });
  }

  function xtreamRequest(creds, action) {
    if (global.webOS && global.webOS.service &&
        typeof global.webOS.service.request === "function") {
      return serviceRequest("xtreamRequest", {
        server: creds.server,
        username: creds.username,
        password: creds.password,
        action: action || ""
      }).then(function (response) {
        if (typeof response.text !== "string") {
          throw new Error("O serviço webOS não retornou JSON Xtream.");
        }
        try {
          return JSON.parse(response.text);
        } catch (error) {
          throw new Error("Provider Xtream retornou JSON inválido.");
        }
      });
    }

    return fetchInBrowser(global.BlazzingXtream.apiUrl(creds, action)).then(function (text) {
      try {
        return JSON.parse(text);
      } catch (error) {
        throw new Error("Provider Xtream retornou JSON inválido.");
      }
    });
  }

  global.BlazzingNetwork = {
    fetchM3U: fetchM3U,
    xtreamRequest: xtreamRequest
  };
}(window));
