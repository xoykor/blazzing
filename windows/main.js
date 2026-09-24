/* SPDX-License-Identifier: GPL-3.0-only */
"use strict";

const { app, BrowserWindow, ipcMain } = require("electron");
const crypto = require("crypto");
const fs = require("fs");
const fsp = fs.promises;
const net = require("net");
const path = require("path");
const { spawn } = require("child_process");

const MAX_RESPONSE_BYTES = 128 * 1024 * 1024;
const PLAYER_CONNECT_TIMEOUT_MS = 7000;

let mainWindow = null;
let mpvProcess = null;
let mpvSocket = null;
let mpvBuffer = "";
let mpvStopping = false;
let currentSession = 0;
let pendingResumeMs = 0;
let recentMpvLog = "";

function rendererSend(channel, payload) {
    if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send(channel, payload);
    }
}

function playerState(text, error) {
    rendererSend("player:state", {
        sessionId: currentSession,
        text: String(text || ""),
        error: !!error
    });
}

function playerTime(milliseconds) {
    rendererSend("player:time", {
        sessionId: currentSession,
        milliseconds: Math.max(0, Math.floor(Number(milliseconds) || 0))
    });
}

function sanitizeLog(text) {
    return String(text || "")
        .replace(/https?:\/\/[^\s"'<>]+/gi, "[URL ocultada]")
        .replace(/[\r\n\t]+/g, " ")
        .trim();
}

function appendMpvLog(text) {
    const clean = sanitizeLog(text);
    if (clean) {
        recentMpvLog = (recentMpvLog + " " + clean).slice(-4096);
    }
}

function validHttpUrl(value) {
    try {
        const parsed = new URL(String(value || ""));
        return parsed.protocol === "http:" || parsed.protocol === "https:";
    } catch (_error) {
        return false;
    }
}

async function requestText(url, options) {
    options = options || {};
    if (!validHttpUrl(url)) {
        throw new Error("URL de rede inválida.");
    }

    const maxBytes = Math.min(
        MAX_RESPONSE_BYTES,
        Math.max(1, Number(options.maxBytes) || MAX_RESPONSE_BYTES)
    );
    const timeout = Math.max(1000, Number(options.timeout) || 60000);
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeout);

    try {
        const response = await fetch(url, {
            method: "GET",
            redirect: "follow",
            signal: controller.signal,
            headers: {
                "User-Agent": "Blazzing/" + app.getVersion() + " Windows"
            }
        });

        if (!response.ok) {
            throw new Error("Servidor respondeu HTTP " + response.status + ".");
        }

        const declared = Number(response.headers.get("content-length")) || 0;
        if (declared > maxBytes) {
            throw new Error("A resposta excede o limite permitido.");
        }
        if (!response.body) {
            return "";
        }

        const reader = response.body.getReader();
        const chunks = [];
        let total = 0;
        for (;;) {
            const part = await reader.read();
            if (part.done) {
                break;
            }
            total += part.value.byteLength;
            if (total > maxBytes) {
                try { await reader.cancel(); } catch (_ignore) {}
                throw new Error("A resposta excede o limite permitido.");
            }
            chunks.push(Buffer.from(part.value));
        }
        return Buffer.concat(chunks, total).toString("utf8");
    } catch (error) {
        if (error && error.name === "AbortError") {
            throw new Error("Tempo limite excedido ao acessar o servidor.");
        }
        throw error;
    } finally {
        clearTimeout(timer);
    }
}

function playlistDirectory() {
    return path.join(app.getPath("userData"), "playlists");
}

function playlistFile(url) {
    const digest = crypto
        .createHash("sha256")
        .update(String(url || ""), "utf8")
        .digest("hex")
        .slice(0, 32);
    return path.join(playlistDirectory(), "playlist-" + digest + ".m3u8");
}

async function readPlaylist(url) {
    const target = playlistFile(url);
    try {
        const stat = await fsp.stat(target);
        const text = await fsp.readFile(target, "utf8");
        return { url: String(url || ""), text, updatedAt: stat.mtimeMs || 0 };
    } catch (error) {
        if (error && error.code === "ENOENT") {
            return null;
        }
        throw error;
    }
}

async function writePlaylist(url, text) {
    url = String(url || "").trim();
    text = String(text || "");
    if (!validHttpUrl(url) || !text) {
        throw new Error("Playlist inválida para armazenamento.");
    }

    const dir = playlistDirectory();
    const target = playlistFile(url);
    const temporary = target + ".tmp-" + process.pid + "-" + Date.now();
    await fsp.mkdir(dir, { recursive: true });
    await fsp.writeFile(temporary, text, "utf8");
    try {
        await fsp.rename(temporary, target);
    } catch (error) {
        if (error && (error.code === "EEXIST" || error.code === "EPERM")) {
            await fsp.rm(target, { force: true });
            await fsp.rename(temporary, target);
        } else {
            await fsp.rm(temporary, { force: true });
            throw error;
        }
    }
    return true;
}

async function deletePlaylist(url) {
    await fsp.rm(playlistFile(url), { force: true });
}

function resolveMpvPath() {
    const configured = String(process.env.VIPTV_MPV_PATH || "").trim();
    if (configured) {
        return configured;
    }

    if (app.isPackaged) {
        const bundled = path.join(process.resourcesPath, "mpv", "mpv.exe");
        if (fs.existsSync(bundled)) {
            return bundled;
        }
    }

    const local = path.join(__dirname, "runtime", "mpv", "mpv.exe");
    if (fs.existsSync(local)) {
        return local;
    }
    return "mpv.exe";
}

async function ensureInputConfig() {
    const file = path.join(app.getPath("userData"), "mpv-input.conf");
    const text = [
        "ESC quit",
        "SPACE cycle pause",
        "LEFT seek -10 relative+exact",
        "RIGHT seek 10 relative+exact",
        "UP add volume 5",
        "DOWN add volume -5",
        "f cycle fullscreen",
        "F cycle fullscreen"
    ].join("\n") + "\n";
    await fsp.writeFile(file, text, "utf8");
    return file;
}

function sendMpv(command) {
    if (!mpvSocket || mpvSocket.destroyed) {
        return false;
    }
    try {
        mpvSocket.write(JSON.stringify({ command }) + "\n");
        return true;
    } catch (_error) {
        return false;
    }
}

function sendObservers() {
    [
        [1, "pause"],
        [2, "time-pos"],
        [3, "duration"],
        [4, "paused-for-cache"],
        [5, "volume"]
    ].forEach((entry) => {
        sendMpv(["observe_property", entry[0], entry[1]]);
    });
}

function handleMpvMessage(message) {
    if (!message || typeof message !== "object") {
        return;
    }
    if (message.event === "start-file") {
        playerState("Abrindo…", false);
        return;
    }
    if (message.event === "file-loaded") {
        if (pendingResumeMs > 30000) {
            sendMpv(["seek", pendingResumeMs / 1000, "absolute+exact"]);
        }
        pendingResumeMs = 0;
        playerState("Reproduzindo", false);
        return;
    }
    if (message.event === "playback-restart") {
        playerState("Reproduzindo", false);
        return;
    }
    if (message.event === "end-file") {
        const reason = String(message.reason || "");
        if (reason === "eof") {
            playerState("Concluído", false);
        } else if (reason !== "stop" && reason !== "quit") {
            playerState(recentMpvLog || "O mpv não conseguiu abrir a mídia.", true);
        }
        return;
    }
    if (message.event !== "property-change") {
        return;
    }
    if (message.name === "time-pos" && typeof message.data === "number") {
        playerTime(message.data * 1000);
    } else if (message.name === "paused-for-cache" && message.data === true) {
        playerState("Buffering…", false);
    } else if (message.name === "pause") {
        playerState(message.data ? "Pausado" : "Reproduzindo", false);
    }
}

function attachMpvSocket(socket) {
    mpvBuffer = "";
    mpvSocket = socket;
    socket.setEncoding("utf8");
    socket.on("data", (chunk) => {
        mpvBuffer += chunk;
        if (mpvBuffer.length > 1024 * 1024) {
            mpvBuffer = mpvBuffer.slice(-65536);
        }
        for (;;) {
            const newline = mpvBuffer.indexOf("\n");
            if (newline < 0) {
                break;
            }
            const line = mpvBuffer.slice(0, newline).trim();
            mpvBuffer = mpvBuffer.slice(newline + 1);
            if (!line) {
                continue;
            }
            try {
                handleMpvMessage(JSON.parse(line));
            } catch (_ignore) {}
        }
    });
    socket.on("error", () => {});
    socket.on("close", () => {
        if (mpvSocket === socket) {
            mpvSocket = null;
        }
    });
}

async function connectMpvPipe(pipePath, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    return new Promise((resolve, reject) => {
        function attempt() {
            if (!mpvProcess || mpvProcess.killed) {
                reject(new Error("O processo mpv encerrou antes de abrir o IPC."));
                return;
            }
            const socket = net.createConnection(pipePath);
            let settled = false;
            socket.once("connect", () => {
                if (!settled) {
                    settled = true;
                    resolve(socket);
                }
            });
            socket.once("error", () => {
                if (settled) {
                    return;
                }
                settled = true;
                socket.destroy();
                if (Date.now() >= deadline) {
                    reject(new Error("O mpv iniciou, mas o IPC não ficou disponível."));
                } else {
                    setTimeout(attempt, 80);
                }
            });
        }
        attempt();
    });
}

function revealCatalog() {
    if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.show();
        mainWindow.focus();
    }
}

async function ensureMpvRuntime() {
    if (mpvProcess && mpvSocket && !mpvSocket.destroyed) {
        return;
    }

    const pipePath = "\\\\.\\pipe\\blazzing-mpv-" + process.pid + "-" + Date.now();
    const inputConf = await ensureInputConfig();
    const executable = resolveMpvPath();
    recentMpvLog = "";
    mpvStopping = false;

    const args = [
        "--no-config",
        "--idle=yes",
        "--force-window=yes",
        "--keep-open=no",
        "--osc=yes",
        "--input-terminal=no",
        "--input-default-bindings=yes",
        "--input-conf=" + inputConf,
        "--hwdec=auto-safe",
        "--audio=auto",
        "--msg-level=all=warn",
        "--input-ipc-server=" + pipePath,
        "--title=Blazzing Player"
    ];

    let child;
    try {
        child = spawn(executable, args, {
            windowsHide: false,
            stdio: ["ignore", "pipe", "pipe"]
        });
        mpvProcess = child;
    } catch (error) {
        throw new Error("Não foi possível iniciar mpv.exe: " + error.message);
    }

    if (child.stdout) {
        child.stdout.on("data", appendMpvLog);
    }
    if (child.stderr) {
        child.stderr.on("data", appendMpvLog);
    }

    child.on("error", (error) => {
        if (mpvProcess === child && !mpvStopping) {
            playerState(
                error && error.code === "ENOENT"
                    ? "mpv.exe não encontrado no pacote."
                    : "Falha ao executar mpv: " + error.message,
                true
            );
            revealCatalog();
        }
    });

    child.on("exit", (code) => {
        if (mpvProcess !== child) {
            return;
        }
        const unexpected = !mpvStopping;
        mpvProcess = null;
        if (mpvSocket) {
            mpvSocket.destroy();
            mpvSocket = null;
        }
        if (unexpected && typeof code === "number" && code !== 0) {
            playerState(recentMpvLog || ("mpv encerrou com código " + code), true);
        }
        revealCatalog();
    });

    const socket = await connectMpvPipe(pipePath, PLAYER_CONNECT_TIMEOUT_MS);
    attachMpvSocket(socket);
    sendObservers();
}

async function openPlayer(payload) {
    payload = payload || {};
    const url = String(payload.url || "");
    if (!validHttpUrl(url)) {
        throw new Error("URL de reprodução inválida.");
    }

    await ensureMpvRuntime();
    currentSession += 1;
    pendingResumeMs = Math.max(0, Number(payload.resumeMs) || 0);
    recentMpvLog = "";

    const command = ["loadfile", url, "replace"];
    const options = {};
    if (payload.referer) {
        options.referrer = String(payload.referer);
    }
    if (payload.userAgent) {
        options["user-agent"] = String(payload.userAgent);
    }
    if (Object.keys(options).length) {
        command.push(-1, options);
    }

    if (!sendMpv(command)) {
        throw new Error("Falha ao enviar mídia ao mpv via IPC.");
    }

    if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.hide();
    }
    playerState("Preparando…", false);
    return { sessionId: currentSession };
}

function stopMpvRuntime() {
    mpvStopping = true;
    pendingResumeMs = 0;
    if (mpvSocket && !mpvSocket.destroyed) {
        try {
            mpvSocket.write(JSON.stringify({ command: ["quit"] }) + "\n");
        } catch (_ignore) {}
        mpvSocket.destroy();
    }
    mpvSocket = null;

    if (mpvProcess) {
        const processToStop = mpvProcess;
        setTimeout(() => {
            if (processToStop && processToStop.exitCode === null) {
                try { processToStop.kill(); } catch (_ignore) {}
            }
        }, 900);
    }
    revealCatalog();
}

function createMainWindow() {
    mainWindow = new BrowserWindow({
        width: 1600,
        height: 900,
        minWidth: 1000,
        minHeight: 650,
        backgroundColor: "#070a12",
        title: "Blazzing",
        autoHideMenuBar: true,
        webPreferences: {
            preload: path.join(__dirname, "preload.js"),
            contextIsolation: true,
            nodeIntegration: false,
            sandbox: true,
            webSecurity: true,
            backgroundThrottling: false
        }
    });

    mainWindow.loadFile(path.join(__dirname, "index.html"));
    mainWindow.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
    mainWindow.webContents.on("will-navigate", (event, url) => {
        if (!url.startsWith("file:")) {
            event.preventDefault();
        }
    });
    mainWindow.webContents.on("before-input-event", (event, input) => {
        if (input.type === "keyDown" && input.key === "F11") {
            event.preventDefault();
            mainWindow.setFullScreen(!mainWindow.isFullScreen());
        }
    });
    mainWindow.on("closed", () => {
        mainWindow = null;
        stopMpvRuntime();
    });
}

ipcMain.handle("app:close", () => {
    app.quit();
    return true;
});
ipcMain.handle("net:text", async (_event, request) => {
    return requestText(request && request.url, request && request.options);
});
ipcMain.handle("net:json", async (_event, request) => {
    const text = await requestText(request && request.url, request && request.options);
    try {
        return JSON.parse(text);
    } catch (_error) {
        throw new Error("O servidor retornou JSON inválido.");
    }
});
ipcMain.handle("playlist:read", async (_event, request) => {
    return readPlaylist(request && request.url);
});
ipcMain.handle("playlist:write", async (_event, request) => {
    return writePlaylist(request && request.url, request && request.text);
});
ipcMain.handle("playlist:delete", async (_event, request) => {
    await deletePlaylist(request && request.url);
    return true;
});
ipcMain.handle("player:open", async (_event, payload) => {
    return openPlayer(payload);
});
ipcMain.handle("player:stop", () => {
    stopMpvRuntime();
    return true;
});
ipcMain.handle("player:command", (_event, request) => {
    const command = request && request.command;
    const value = request && request.value;
    if (command === "togglePause") {
        return sendMpv(["cycle", "pause"]);
    }
    if (command === "seek") {
        return sendMpv(["seek", Number(value) || 0, "relative+exact"]);
    }
    if (command === "volume") {
        return sendMpv(["set_property", "volume", Math.max(0, Math.min(130, Number(value) || 0))]);
    }
    if (command === "adjustVolume") {
        return sendMpv(["add", "volume", Number(value) || 0]);
    }
    return false;
});

app.setAppUserModelId("io.github.xoykor.Blazzing");
app.whenReady().then(() => {
    createMainWindow();
    app.on("activate", () => {
        if (BrowserWindow.getAllWindows().length === 0) {
            createMainWindow();
        }
    });
});
app.on("window-all-closed", () => {
    app.quit();
});
app.on("before-quit", () => {
    stopMpvRuntime();
});
