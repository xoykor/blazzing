/* SPDX-License-Identifier: MIT */
/*
 * LG webOS compatibility layer.
 *
 * The TV frontend is shared with the Tizen port wherever possible.  The main
 * controller already understands Samsung-style key names, so this adapter
 * exposes only the tiny subset it needs: remote key discovery/registration
 * and application exit. Playback itself stays on the standard HTML5 <video>
 * element, which lets webOS use its native media pipeline.
 */
(function () {
    "use strict";

    var keys = [
        { name: "Back", code: 461 },
        { name: "MediaPlay", code: 415 },
        { name: "MediaPause", code: 19 },
        { name: "MediaStop", code: 413 },
        { name: "MediaPlayPause", code: 179 },
        { name: "MediaFastForward", code: 417 },
        { name: "MediaRewind", code: 412 }
    ];

    /*
     * Keep the shared controller unchanged by providing the minimal Tizen
     * facade it already consumes.  Nothing here attempts to emulate Samsung
     * AVPlay; the player module will naturally select its HTML5 fallback.
     */
    if (!window.tizen) {
        window.tizen = {
            tvinputdevice: {
                getSupportedKeys: function () {
                    return keys.slice();
                },
                registerKeyBatch: function () {},
                registerKey: function () {}
            },
            application: {
                getCurrentApplication: function () {
                    return {
                        exit: function () {
                            try { window.close(); } catch (ignoreClose) {}
                        }
                    };
                }
            }
        };
    }

    window.BlazzingPlatform = {
        name: "webOS",
        isWebOS: true,
        exit: function () {
            try { window.close(); } catch (ignoreClose) {}
        }
    };

    document.addEventListener("DOMContentLoaded", function () {
        var badge = document.getElementById("platform-badge");
        if (badge) {
            badge.textContent = "LG webOS · HTML5";
        }
    });
}());
