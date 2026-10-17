/* SPDX-License-Identifier: MIT */
"use strict";

function createMockElement(id) {
    var listeners = {};
    return {
        id: id || "",
        value: "",
        checked: false,
        className: "",
        classList: {
            add: function () {},
            remove: function () {},
            toggle: function () {}
        },
        parentNode: {
            addEventListener: function () {}
        },
        setAttribute: function () {},
        getAttribute: function () { return ""; },
        appendChild: function () {},
        addEventListener: function (type, fn) {
            listeners[type] = fn;
        },
        querySelectorAll: function () {
            return [];
        },
        querySelector: function () {
            return null;
        },
        focus: function () {}
    };
}

var elements = {};
function getOrCreateElement(id) {
    if (!elements[id]) {
        elements[id] = createMockElement(id);
    }
    return elements[id];
}

global.document = {
    getElementById: function (id) {
        return getOrCreateElement(id);
    },
    querySelectorAll: function () {
        return [];
    },
    querySelector: function () {
        return null;
    },
    createElement: function (tag) {
        return createMockElement(tag);
    },
    addEventListener: function () {}
};

global.window = {
    addEventListener: function () {},
    localStorage: {
        getItem: function () { return null; },
        setItem: function () {}
    },
    BlazzingStorage: {
        profiles: function () { return []; },
        lastOpenedProfileId: function () { return ""; },
        isFavorite: function () { return false; },
        saveProfile: function (p) { return p; }
    },
    BlazzingPlayer: {
        on: function () {},
        stop: function () {},
        setStateListener: function () {},
        setTimeListener: function () {}
    },
    BlazzingProviders: {},
    BlazzingBoot: {
        markReady: function () {}
    }
};

require("../js/app.js");

console.log("Tizen app boot & scope test: OK");
