/* SPDX-License-Identifier: MIT */
/*
 * Very early boot diagnostics.
 *
 * This file is intentionally loaded before Samsung WebAPIs and the application
 * controller. If a JavaScript exception prevents the normal UI from starting,
 * the TV shows the error instead of leaving a blank screen.
 */
(function () {
    "use strict";

    var pending = [];
    var ready = false;

    function stringify(value) {
        if (value instanceof Error) {
            return value.name + ": " + value.message +
                (value.stack ? "\n" + value.stack : "");
        }
        try {
            return typeof value === "string" ? value : JSON.stringify(value);
        } catch (ignore) {
            return String(value);
        }
    }

    function show(message) {
        var overlay;
        var pre;

        pending.push(message);
        if (!ready || !document.body) {
            return;
        }

        overlay = document.getElementById("boot-error");
        if (!overlay) {
            overlay = document.createElement("div");
            overlay.id = "boot-error";
            overlay.style.position = "fixed";
            overlay.style.left = "48px";
            overlay.style.right = "48px";
            overlay.style.bottom = "48px";
            overlay.style.zIndex = "10000";
            overlay.style.padding = "28px";
            overlay.style.borderRadius = "18px";
            overlay.style.background = "rgba(95, 15, 15, .97)";
            overlay.style.color = "#fff";
            overlay.style.fontFamily = "monospace";
            overlay.style.fontSize = "18px";
            overlay.style.whiteSpace = "pre-wrap";
            overlay.style.maxHeight = "45vh";
            overlay.style.overflow = "auto";

            pre = document.createElement("div");
            pre.id = "boot-error-text";
            overlay.appendChild(pre);
            document.body.appendChild(overlay);
        }

        pre = document.getElementById("boot-error-text");
        pre.textContent = "Blazzing — erro de inicialização\n\n" +
            pending.join("\n\n");
    }

    window.addEventListener("error", function (event) {
        show(
            (event.message || "Erro JavaScript") +
            (event.filename ? "\n" + event.filename + ":" +
                event.lineno + ":" + event.colno : "")
        );
    });

    window.addEventListener("unhandledrejection", function (event) {
        show("Promise rejeitada: " + stringify(event.reason));
    });

    document.addEventListener("DOMContentLoaded", function () {
        ready = true;
        if (pending.length) {
            var saved = pending.slice();
            pending = [];
            saved.forEach(show);
        }
    });

    window.BlazzingBoot = {
        report: show,
        markReady: function () {
            var overlay = document.getElementById("boot-error");
            if (overlay && overlay.parentNode) {
                overlay.parentNode.removeChild(overlay);
            }
        }
    };
}());
