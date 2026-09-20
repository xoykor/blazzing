/* SPDX-License-Identifier: MIT */
/*
 * Secure phone pairing for the Samsung TV port.
 *
 * Protocol compatibility:
 *   1. TV creates a random 128-bit session id and 256-bit AES-GCM key.
 *   2. Only the session id reaches the Worker during session creation.
 *   3. The QR contains /pair/<id>#<key>. URL fragments are not sent to HTTP servers.
 *   4. The phone encrypts {name,url} in the browser and uploads only ciphertext.
 *   5. The TV polls the relay and decrypts locally.
 *
 * The QR encoder below is intentionally local so the AES key is never disclosed
 * to a third-party QR service. It emits QR Code Model 2, version 6-L, byte mode.
 */
(function () {
    "use strict";

    var DEFAULT_BASE_URL = "https://blazzing-pairing.vsxk.workers.dev";
    var POLL_MS = 1000;
    var TTL_MS = 5 * 60 * 1000;
    var current = null;

    function randomBytes(size) {
        var bytes = new Uint8Array(size);
        window.crypto.getRandomValues(bytes);
        return bytes;
    }

    function bytesToHex(bytes) {
        var out = "";
        var i;
        for (i = 0; i < bytes.length; i += 1) {
            out += ("0" + bytes[i].toString(16)).slice(-2);
        }
        return out;
    }

    function bytesToBase64url(bytes) {
        var raw = "";
        var i;
        for (i = 0; i < bytes.length; i += 1) {
            raw += String.fromCharCode(bytes[i]);
        }
        return window.btoa(raw)
            .replace(/\+/g, "-")
            .replace(/\//g, "_")
            .replace(/=+$/g, "");
    }

    function base64urlToBytes(text) {
        var base64 = String(text || "").replace(/-/g, "+").replace(/_/g, "/");
        var raw;
        var bytes;
        var i;

        while (base64.length % 4) {
            base64 += "=";
        }

        raw = window.atob(base64);
        bytes = new Uint8Array(raw.length);
        for (i = 0; i < raw.length; i += 1) {
            bytes[i] = raw.charCodeAt(i);
        }
        return bytes;
    }

    function request(method, url, body, timeout) {
        return new Promise(function (resolve, reject) {
            var xhr = new XMLHttpRequest();
            var finished = false;

            function fail(message) {
                if (finished) { return; }
                finished = true;
                reject(new Error(message));
            }

            xhr.open(method, url, true);
            xhr.timeout = timeout || 10000;
            xhr.withCredentials = false;

            if (body !== undefined && body !== null) {
                xhr.setRequestHeader("Content-Type", "application/json");
            }

            xhr.onload = function () {
                if (finished) { return; }
                finished = true;
                resolve({
                    status: xhr.status,
                    text: xhr.responseText || ""
                });
            };
            xhr.onerror = function () { fail("Falha de rede no pareamento."); };
            xhr.ontimeout = function () { fail("O relay demorou demais para responder."); };
            xhr.send(body === undefined ? null : body);
        });
    }

    /* ---------- Small QR Code version 6-L encoder ---------- */

    function gfMultiply(x, y) {
        var z = 0;
        var i;
        for (i = 7; i >= 0; i -= 1) {
            z = (z << 1) ^ ((z >>> 7) * 0x11D);
            z ^= ((y >>> i) & 1) * x;
        }
        return z & 0xFF;
    }

    function rsDivisor(degree) {
        var result = [];
        var root = 1;
        var i;
        var j;

        for (i = 0; i < degree; i += 1) {
            result.push(0);
        }
        result[degree - 1] = 1;

        for (i = 0; i < degree; i += 1) {
            for (j = 0; j < degree; j += 1) {
                result[j] = gfMultiply(result[j], root);
                if (j + 1 < degree) {
                    result[j] ^= result[j + 1];
                }
            }
            root = gfMultiply(root, 2);
        }
        return result;
    }

    function rsRemainder(data, divisor) {
        var result = [];
        var i;
        var j;
        var factor;

        for (i = 0; i < divisor.length; i += 1) {
            result.push(0);
        }

        for (i = 0; i < data.length; i += 1) {
            factor = data[i] ^ result[0];
            result.shift();
            result.push(0);
            for (j = 0; j < result.length; j += 1) {
                result[j] ^= gfMultiply(divisor[j], factor);
            }
        }
        return result;
    }

    function appendBits(target, value, length) {
        var i;
        for (i = length - 1; i >= 0; i -= 1) {
            target.push((value >>> i) & 1);
        }
    }

    function utf8Bytes(text) {
        if (window.TextEncoder) {
            return Array.prototype.slice.call(new TextEncoder().encode(text));
        }

        /* Tizen 9 has TextEncoder, but keep a standards-compatible fallback. */
        var encoded = unescape(encodeURIComponent(text));
        var out = [];
        var i;
        for (i = 0; i < encoded.length; i += 1) {
            out.push(encoded.charCodeAt(i));
        }
        return out;
    }

    function qrCodewords(text) {
        var raw = utf8Bytes(text);
        var bits = [];
        var data = [];
        var pads = [0xEC, 0x11];
        var padIndex = 0;
        var capacityBits = 136 * 8;
        var i;
        var j;
        var value;
        var blocks;
        var ecc;
        var divisor;
        var output = [];

        /* Version 6-L byte capacity is 134 bytes. */
        if (raw.length > 134) {
            throw new Error("URL de pareamento longa demais para o QR local.");
        }

        appendBits(bits, 0x4, 4);       /* byte mode */
        appendBits(bits, raw.length, 8); /* versions 1-9 use 8-bit count */

        for (i = 0; i < raw.length; i += 1) {
            appendBits(bits, raw[i], 8);
        }

        for (i = 0; i < 4 && bits.length < capacityBits; i += 1) {
            bits.push(0);
        }
        while (bits.length % 8) {
            bits.push(0);
        }

        for (i = 0; i < bits.length; i += 8) {
            value = 0;
            for (j = 0; j < 8; j += 1) {
                value = (value << 1) | bits[i + j];
            }
            data.push(value);
        }

        while (data.length < 136) {
            data.push(pads[padIndex % 2]);
            padIndex += 1;
        }

        /* Version 6-L: 2 blocks, each 68 data + 18 EC codewords. */
        blocks = [data.slice(0, 68), data.slice(68, 136)];
        divisor = rsDivisor(18);
        ecc = [
            rsRemainder(blocks[0], divisor),
            rsRemainder(blocks[1], divisor)
        ];

        for (i = 0; i < 68; i += 1) {
            output.push(blocks[0][i]);
            output.push(blocks[1][i]);
        }
        for (i = 0; i < 18; i += 1) {
            output.push(ecc[0][i]);
            output.push(ecc[1][i]);
        }
        return output;
    }

    function qrFormatBits(mask) {
        var data = (1 << 3) | mask; /* L = 01 */
        var rem = data;
        var i;

        for (i = 0; i < 10; i += 1) {
            rem = (rem << 1) ^ ((rem >>> 9) * 0x537);
        }
        return ((data << 10) | rem) ^ 0x5412;
    }

    function qrMatrix(text) {
        var size = 41; /* version 6 */
        var matrix = [];
        var isFunction = [];
        var codewords = qrCodewords(text);
        var dataBits = [];
        var mask = 0;
        var format = qrFormatBits(mask);
        var bitIndex = 0;
        var x;
        var y;
        var i;
        var j;
        var right;
        var vert;
        var upward;
        var raw;
        var dark;

        for (y = 0; y < size; y += 1) {
            matrix[y] = [];
            isFunction[y] = [];
            for (x = 0; x < size; x += 1) {
                matrix[y][x] = false;
                isFunction[y][x] = false;
            }
        }

        function setFunction(px, py, value) {
            if (px >= 0 && px < size && py >= 0 && py < size) {
                matrix[py][px] = !!value;
                isFunction[py][px] = true;
            }
        }

        function finder(cx, cy) {
            var dx;
            var dy;
            var dist;
            for (dy = -4; dy <= 4; dy += 1) {
                for (dx = -4; dx <= 4; dx += 1) {
                    if (cx + dx >= 0 && cx + dx < size &&
                            cy + dy >= 0 && cy + dy < size) {
                        dist = Math.max(Math.abs(dx), Math.abs(dy));
                        setFunction(cx + dx, cy + dy, dist !== 2 && dist !== 4);
                    }
                }
            }
        }

        function alignment(cx, cy) {
            var dx;
            var dy;
            for (dy = -2; dy <= 2; dy += 1) {
                for (dx = -2; dx <= 2; dx += 1) {
                    setFunction(
                        cx + dx,
                        cy + dy,
                        Math.max(Math.abs(dx), Math.abs(dy)) !== 1
                    );
                }
            }
        }

        function formatBit(index) {
            return ((format >>> index) & 1) !== 0;
        }

        for (i = 0; i < size; i += 1) {
            setFunction(6, i, i % 2 === 0);
            setFunction(i, 6, i % 2 === 0);
        }

        finder(3, 3);
        finder(size - 4, 3);
        finder(3, size - 4);

        /* Version 6 alignment centers are 6 and 34; only (34,34) is free. */
        alignment(34, 34);

        for (i = 0; i < 6; i += 1) {
            setFunction(8, i, formatBit(i));
        }
        setFunction(8, 7, formatBit(6));
        setFunction(8, 8, formatBit(7));
        setFunction(7, 8, formatBit(8));
        for (i = 9; i < 15; i += 1) {
            setFunction(14 - i, 8, formatBit(i));
        }
        for (i = 0; i < 8; i += 1) {
            setFunction(size - 1 - i, 8, formatBit(i));
        }
        for (i = 8; i < 15; i += 1) {
            setFunction(8, size - 15 + i, formatBit(i));
        }
        setFunction(8, size - 8, true); /* fixed dark module */

        for (i = 0; i < codewords.length; i += 1) {
            appendBits(dataBits, codewords[i], 8);
        }

        right = size - 1;
        while (right >= 1) {
            if (right === 6) {
                right = 5;
            }

            for (vert = 0; vert < size; vert += 1) {
                upward = ((right + 1) & 2) === 0;
                y = upward ? size - 1 - vert : vert;

                for (j = 0; j < 2; j += 1) {
                    x = right - j;
                    if (!isFunction[y][x]) {
                        raw = bitIndex < dataBits.length ? dataBits[bitIndex] : 0;
                        if (bitIndex < dataBits.length) {
                            bitIndex += 1;
                        }
                        dark = raw === 1;

                        /* Mask pattern 0. Remainder modules are masked as zero bits. */
                        if ((x + y) % 2 === 0) {
                            dark = !dark;
                        }
                        matrix[y][x] = dark;
                    }
                }
            }
            right -= 2;
        }

        return matrix;
    }

    function qrSvg(text) {
        var matrix = qrMatrix(text);
        var border = 4;
        var size = matrix.length + border * 2;
        var path = "";
        var x;
        var y;

        for (y = 0; y < matrix.length; y += 1) {
            for (x = 0; x < matrix.length; x += 1) {
                if (matrix[y][x]) {
                    path += "M" + (x + border) + " " + (y + border) + "h1v1h-1z";
                }
            }
        }

        return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ' +
            size + " " + size + '" role="img" aria-label="QR de pareamento" ' +
            'shape-rendering="crispEdges">' +
            '<rect width="100%" height="100%" fill="#fff"/>' +
            '<path d="' + path + '" fill="#000"/></svg>';
    }

    function decryptPayload(keyBytes, payloadText) {
        var payload;

        try {
            payload = JSON.parse(payloadText);
        } catch (error) {
            return Promise.reject(new Error("O relay retornou dados inválidos."));
        }

        if (!payload || typeof payload.iv !== "string" ||
                typeof payload.ciphertext !== "string") {
            return Promise.reject(new Error("Payload de pareamento incompleto."));
        }

        return window.crypto.subtle.importKey(
            "raw",
            keyBytes,
            { name: "AES-GCM" },
            false,
            ["decrypt"]
        ).then(function (key) {
            return window.crypto.subtle.decrypt(
                {
                    name: "AES-GCM",
                    iv: base64urlToBytes(payload.iv)
                },
                key,
                base64urlToBytes(payload.ciphertext)
            );
        }).then(function (clearBuffer) {
            var clear;

            if (window.TextDecoder) {
                clear = new TextDecoder("utf-8").decode(clearBuffer);
            } else {
                var bytes = new Uint8Array(clearBuffer);
                var binary = "";
                var i;
                for (i = 0; i < bytes.length; i += 1) {
                    binary += String.fromCharCode(bytes[i]);
                }
                clear = decodeURIComponent(escape(binary));
            }

            var data = JSON.parse(clear);
            if (!data || !/^https?:\/\//i.test(String(data.url || ""))) {
                throw new Error("A URL recebida do celular é inválida.");
            }

            return {
                name: String(data.name || "").slice(0, 127),
                url: String(data.url)
            };
        });
    }

    function cleanupRemote(session) {
        if (!session) { return; }
        request(
            "DELETE",
            session.baseUrl + "/api/v1/sessions/" + session.id,
            null,
            4000
        ).catch(function () {});
    }

    function stop() {
        var session = current;
        if (!session) { return; }
        current = null;
        session.cancelled = true;
        if (session.timer) {
            clearTimeout(session.timer);
        }
        cleanupRemote(session);
    }

    function poll(session, onSubmit, onState) {
        if (current !== session || session.cancelled) {
            return;
        }

        if (Date.now() >= session.deadline) {
            current = null;
            cleanupRemote(session);
            onState("A sessão expirou. Gere um novo QR.", "error");
            return;
        }

        request(
            "GET",
            session.baseUrl + "/api/v1/sessions/" + session.id + "/payload",
            null,
            8000
        ).then(function (response) {
            if (current !== session || session.cancelled) {
                return;
            }

            if (response.status === 204) {
                onState("Aguardando envio pelo celular…", "waiting");
                session.timer = setTimeout(function () {
                    poll(session, onSubmit, onState);
                }, POLL_MS);
                return;
            }

            if (response.status === 410) {
                current = null;
                onState("A sessão expirou. Gere um novo QR.", "error");
                return;
            }

            if (response.status !== 200) {
                throw new Error("O relay retornou HTTP " + response.status + ".");
            }

            return decryptPayload(session.key, response.text).then(function (profile) {
                if (current !== session || session.cancelled) {
                    return;
                }
                current = null;
                cleanupRemote(session);
                onState("Playlist recebida.", "done");
                onSubmit(profile);
            });
        }).catch(function (error) {
            if (current !== session || session.cancelled) {
                return;
            }
            onState(error.message || "Falha temporária no pareamento.", "error");
            session.timer = setTimeout(function () {
                poll(session, onSubmit, onState);
            }, 1800);
        });
    }

    function start(onSubmit, onState, baseUrl) {
        var session;
        var id;
        var key;
        var pageUrl;

        stop();

        if (!window.crypto || !window.crypto.getRandomValues ||
                !window.crypto.subtle) {
            return Promise.reject(
                new Error("Web Crypto não está disponível nesta TV.")
            );
        }

        baseUrl = String(baseUrl || DEFAULT_BASE_URL).replace(/\/+$/, "");
        id = bytesToHex(randomBytes(16));
        key = randomBytes(32);
        pageUrl = baseUrl + "/pair/" + id + "#" + bytesToBase64url(key);

        /* Generate first so a too-long custom relay URL fails before creating a session. */
        qrMatrix(pageUrl);

        session = {
            id: id,
            key: key,
            baseUrl: baseUrl,
            pageUrl: pageUrl,
            deadline: Date.now() + TTL_MS,
            timer: 0,
            cancelled: false
        };
        current = session;

        onState("Criando sessão segura…", "creating");

        return request(
            "POST",
            baseUrl + "/api/v1/sessions/" + id,
            null,
            10000
        ).then(function (response) {
            if (current !== session || session.cancelled) {
                throw new Error("Pareamento cancelado.");
            }
            if (response.status !== 201) {
                throw new Error("Não foi possível criar a sessão (HTTP " +
                    response.status + ").");
            }

            onState("Aponte a câmera do celular para o QR.", "ready");
            poll(session, onSubmit, onState);

            return {
                url: pageUrl,
                qrSvg: qrSvg(pageUrl),
                sessionId: id
            };
        }).catch(function (error) {
            if (current === session) {
                current = null;
            }
            cleanupRemote(session);
            throw error;
        });
    }

    window.BlazzingPairing = {
        start: start,
        stop: stop,
        qrSvg: qrSvg,
        _qrMatrix: qrMatrix
    };
}());
