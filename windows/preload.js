/* SPDX-License-Identifier: GPL-3.0-only */
"use strict";

const { contextBridge, ipcRenderer } = require("electron");

function subscribe(channel, callback) {
    if (typeof callback !== "function") {
        return function () {};
    }
    const listener = (_event, payload) => callback(payload);
    ipcRenderer.on(channel, listener);
    return function () {
        ipcRenderer.removeListener(channel, listener);
    };
}

contextBridge.exposeInMainWorld("BlazzingWindowsNative", {
    close: () => ipcRenderer.invoke("app:close"),
    netText: (url, options) => ipcRenderer.invoke("net:text", { url, options }),
    netJson: (url, options) => ipcRenderer.invoke("net:json", { url, options }),
    cachedPlaylist: (url) => ipcRenderer.invoke("playlist:read", { url }),
    saveCachedPlaylist: (url, text) => ipcRenderer.invoke("playlist:write", { url, text }),
    deleteCachedPlaylist: (url) => ipcRenderer.invoke("playlist:delete", { url }),
    playerOpen: (payload) => ipcRenderer.invoke("player:open", payload),
    playerStop: () => ipcRenderer.invoke("player:stop"),
    playerCommand: (command, value) => ipcRenderer.invoke("player:command", { command, value }),
    onPlayerState: (callback) => subscribe("player:state", callback),
    onPlayerTime: (callback) => subscribe("player:time", callback),
    onPlayerKey: (callback) => subscribe("player:key", callback)
});
