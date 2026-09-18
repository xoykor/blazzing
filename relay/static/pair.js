// SPDX-License-Identifier: MIT
(() => {
  "use strict";

  const sendButton = document.getElementById("send");
  const nameInput = document.getElementById("name");
  const urlInput = document.getElementById("url");
  const status = document.getElementById("status");

  function setStatus(message, kind = "") {
    status.textContent = message;
    status.className = "status" + (kind ? " " + kind : "");
  }

  function base64urlToBytes(text) {
    if (!/^[A-Za-z0-9_-]{43}$/.test(text)) {
      throw new Error("Chave de pareamento inválida.");
    }
    const base64 = text.replace(/-/g, "+").replace(/_/g, "/") + "=";
    const raw = atob(base64);
    const bytes = new Uint8Array(raw.length);
    for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
    return bytes;
  }

  function bytesToBase64url(bytes) {
    let raw = "";
    for (const byte of bytes) raw += String.fromCharCode(byte);
    return btoa(raw).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
  }

  function sessionID() {
    const match = location.pathname.match(/^\/pair\/([a-f0-9]{32})$/);
    if (!match) throw new Error("Sessão de pareamento inválida.");
    return match[1];
  }

  async function encryptPayload(keyBytes, payload) {
    const key = await crypto.subtle.importKey(
      "raw", keyBytes, { name: "AES-GCM" }, false, ["encrypt"]
    );
    const iv = crypto.getRandomValues(new Uint8Array(12));
    const clear = new TextEncoder().encode(JSON.stringify(payload));
    const encrypted = await crypto.subtle.encrypt({ name: "AES-GCM", iv }, key, clear);
    return {
      iv: bytesToBase64url(iv),
      ciphertext: bytesToBase64url(new Uint8Array(encrypted))
    };
  }

  async function submit() {
    const url = urlInput.value.trim();
    if (!/^https?:\/\//i.test(url)) {
      setStatus("Informe uma URL HTTP/HTTPS válida.", "err");
      return;
    }

    sendButton.disabled = true;
    setStatus("Criptografando e enviando…");
    try {
      const id = sessionID();
      const keyText = location.hash.startsWith("#") ? location.hash.slice(1) : "";
      const keyBytes = base64urlToBytes(keyText);
      const body = await encryptPayload(keyBytes, {
        name: nameInput.value.trim(),
        url
      });

      const response = await fetch(`/api/v1/sessions/${id}/payload`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
        cache: "no-store",
        credentials: "omit"
      });

      if (response.status === 204) {
        setStatus("Enviado. Volte para o Blazzing.", "ok");
        nameInput.disabled = true;
        urlInput.disabled = true;
        sendButton.textContent = "Enviado";
        return;
      }
      if (response.status === 410) throw new Error("A sessão expirou. Gere um novo QR no Blazzing.");
      if (response.status === 409) throw new Error("Esta sessão já recebeu uma playlist.");
      throw new Error("O relay recusou o envio.");
    } catch (error) {
      setStatus(error instanceof Error ? error.message : "Falha no pareamento.", "err");
      sendButton.disabled = false;
    }
  }

  if (!window.crypto || !window.crypto.subtle) {
    setStatus("Este navegador não oferece Web Crypto. Abra o QR em um navegador HTTPS moderno.", "err");
    sendButton.disabled = true;
    return;
  }

  sendButton.addEventListener("click", submit);
  urlInput.addEventListener("keydown", (event) => {
    if (event.key === "Enter") submit();
  });
})();
