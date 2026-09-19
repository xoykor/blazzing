/* SPDX-License-Identifier: MIT */
"use strict";

global.window = {
    TextEncoder: global.TextEncoder,
    TextDecoder: global.TextDecoder,
    btoa: function (value) { return Buffer.from(value, "binary").toString("base64"); },
    atob: function (value) { return Buffer.from(value, "base64").toString("binary"); }
};

require("../js/pairing.js");

var pairing = global.window.BlazzingPairing;

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

var url = "https://blazzing-pairing.vsxk.workers.dev/pair/" +
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa#" +
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

var matrix = pairing._qrMatrix(url);
assert(matrix.length === 41, "QR v6 deveria ter 41 módulos");
assert(matrix.every(function (row) { return row.length === 41; }),
    "todas as linhas do QR deveriam ter 41 módulos");

var dark = 0;
var hash = 2166136261;
matrix.forEach(function (row) {
    row.forEach(function (cell) {
        var bit = cell ? 1 : 0;
        if (bit) { dark += 1; }
        hash ^= bit;
        hash = Math.imul(hash, 16777619) >>> 0;
    });
});

assert(dark === 854, "matriz QR inesperada: módulos escuros divergiram");
assert(hash === 0x75d6ae1f, "matriz QR inesperada: checksum divergiu");

var svg = pairing.qrSvg(url);
assert(svg.indexOf("<svg") !== -1 && svg.indexOf("<path") !== -1,
    "QR deveria ser renderizado como SVG local");
assert(svg.indexOf(url) === -1,
    "a URL/chave não deve aparecer como texto dentro do SVG");

console.log("Tizen pairing QR tests: OK");
