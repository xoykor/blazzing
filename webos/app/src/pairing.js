(function (global) {
  "use strict";

  function randomBytes(length) {
    var bytes = new Uint8Array(length);
    global.crypto.getRandomValues(bytes);
    return bytes;
  }

  function hex(bytes) {
    var out = "";
    var i;
    for (i = 0; i < bytes.length; i += 1) {
      out += ("0" + bytes[i].toString(16)).slice(-2);
    }
    return out;
  }

  function base64url(bytes) {
    var raw = "";
    var i;
    for (i = 0; i < bytes.length; i += 1) {
      raw += String.fromCharCode(bytes[i]);
    }
    return btoa(raw)
      .replace(/\+/g, "-")
      .replace(/\//g, "_")
      .replace(/=+$/g, "");
  }

  function decodeBase64url(text) {
    var base64 = text.replace(/-/g, "+").replace(/_/g, "/");
    var pad = base64.length % 4;
    var raw;
    var bytes;
    var i;

    if (pad) {
      base64 += new Array(5 - pad).join("=");
    }

    raw = atob(base64);
    bytes = new Uint8Array(raw.length);
    for (i = 0; i < raw.length; i += 1) {
      bytes[i] = raw.charCodeAt(i);
    }
    return bytes;
  }

  function clearBytes(bytes) {
    var i;
    for (i = 0; i < bytes.length; i += 1) {
      bytes[i] = 0;
    }
  }

  function decrypt(keyBytes, payload) {
    var iv = decodeBase64url(payload.iv);
    var ciphertext = decodeBase64url(payload.ciphertext);

    return global.crypto.subtle.importKey(
      "raw",
      keyBytes,
      { name: "AES-GCM" },
      false,
      ["decrypt"]
    ).then(function (key) {
      return global.crypto.subtle.decrypt(
        { name: "AES-GCM", iv: iv },
        key,
        ciphertext
      );
    }).then(function (plainBuffer) {
      var text = new TextDecoder("utf-8").decode(new Uint8Array(plainBuffer));
      return JSON.parse(text);
    });
  }

  function start(options) {
    var config = global.BlazzingConfig;
    var relay = config.pairingRelay.replace(/\/$/, "");
    var sessionId = hex(randomBytes(16));
    var keyBytes = randomBytes(32);
    var pageUrl = relay + "/pair/" + sessionId + "#" + base64url(keyBytes);
    var stopped = false;
    var timer = null;
    var deadline = Date.now() + config.pairingTtlMs;

    function status(message) {
      if (options.onStatus) {
        options.onStatus(message);
      }
    }

    function removeRemoteSession() {
      return fetch(relay + "/api/v1/sessions/" + sessionId, {
        method: "DELETE",
        cache: "no-store",
        credentials: "omit"
      }).catch(function () {
        return null;
      });
    }

    function finish() {
      stopped = true;
      if (timer !== null) {
        clearTimeout(timer);
        timer = null;
      }
      clearBytes(keyBytes);
    }

    function stop() {
      if (stopped) {
        return Promise.resolve();
      }
      finish();
      return removeRemoteSession();
    }

    function poll() {
      if (stopped) {
        return;
      }

      if (Date.now() >= deadline) {
        status("Sessão expirada. Gere uma nova sessão.");
        removeRemoteSession().then(finish);
        if (options.onExpired) {
          options.onExpired();
        }
        return;
      }

      fetch(relay + "/api/v1/sessions/" + sessionId + "/payload", {
        method: "GET",
        cache: "no-store",
        credentials: "omit"
      }).then(function (response) {
        if (response.status === 204) {
          timer = setTimeout(poll, config.pairingPollMs);
          return null;
        }

        if (response.status === 410) {
          throw new Error("A sessão expirou.");
        }

        if (response.status !== 200) {
          throw new Error("Falha ao consultar o pareamento.");
        }

        return response.json();
      }).then(function (payload) {
        if (!payload || stopped) {
          return;
        }

        return decrypt(keyBytes, payload).then(function (clearPayload) {
          return removeRemoteSession().then(function () {
            finish();
            if (options.onPayload) {
              options.onPayload(clearPayload);
            }
          });
        });
      }).catch(function (error) {
        if (stopped) {
          return;
        }
        status(error && error.message ? error.message : "Falha no pareamento.");
        finish();
        if (options.onError) {
          options.onError(error);
        }
      });
    }

    if (!global.crypto || !global.crypto.subtle || !global.crypto.getRandomValues) {
      return Promise.reject(new Error("Web Crypto não está disponível nesta TV."));
    }

    return fetch(relay + "/api/v1/sessions/" + sessionId, {
      method: "POST",
      cache: "no-store",
      credentials: "omit"
    }).then(function (response) {
      if (response.status !== 201) {
        throw new Error("Não foi possível criar a sessão de pareamento.");
      }

      status("Aguardando envio pelo celular…");
      timer = setTimeout(poll, config.pairingPollMs);

      return {
        sessionId: sessionId,
        pageUrl: pageUrl,
        stop: stop
      };
    }).catch(function (error) {
      clearBytes(keyBytes);
      throw error;
    });
  }

  global.BlazzingPairing = {
    start: start
  };
}(window));
