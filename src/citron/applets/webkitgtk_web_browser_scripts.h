// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// WEBKITGTK_NX_SCRIPT is the only backend-specific script here now -- the other
// 4 (fonts, focus-link, gamepad) were byte-identical to the WebView2 copies and
// live in web_browser_scripts_common.h instead.

#pragma once

#include "citron/applets/web_browser_scripts_common.h"

constexpr char WEBKITGTK_NX_SCRIPT[] = R"(
// Ported from WINDOW_NX_SCRIPT (qt_web_browser_scripts.h). Two mechanism changes
// vs the original, both to replace main.cpp's poll loop with push-based delivery:
//
// 1. sendMessage(): posts directly via window.webkit.messageHandlers.nxMessage
//    instead of pushing onto citron_outgoing_messages for main.cpp to drain.
//
// 2. endApplet(): posts via nxControl instead of setting an end_applet boolean
//    that main.cpp polled via runJavaScript.
//
// citron_key_callbacks and everything else are unchanged.

var citron_key_callbacks = [];

(function() {
    class WindowNX {
        constructor() {
            citron_key_callbacks[1] = function() {
                if (window.history.length > 2) {
                    window.history.back();
                } else {
                    window.nx.endApplet();
                }
            };
            citron_key_callbacks[2] = function() { window.nx.endApplet(); };
        }

        addEventListener(type, listener, options) {
            console.log("nx.addEventListener called, type=%s", type);

            window.addEventListener(type, listener, options);
        }

        endApplet() {
            console.log("nx.endApplet called");

            if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.nxControl) {
                window.webkit.messageHandlers.nxControl.postMessage(JSON.stringify({event: "endApplet"}));
            } else {
                console.log("nx.endApplet: nxControl message handler not registered, dropping");
            }
        }

        playSystemSe(system_se) {
            console.log("nx.playSystemSe is not implemented, system_se=%s", system_se);
        }

        sendMessage(message) {
            console.log("nx.sendMessage called, message=%s", message);

            if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.nxMessage) {
                window.webkit.messageHandlers.nxMessage.postMessage(typeof message === "string" ? message : JSON.stringify(message));
            } else {
                console.log("nx.sendMessage: nxMessage message handler not registered, dropping");
            }
        }

        setCursorScrollSpeed(scroll_speed) {
            console.log("nx.setCursorScrollSpeed is not implemented, scroll_speed=%d", scroll_speed);
        }
    }

    class WindowNXFooter {
        setAssign(key, label, func, option) {
            console.log("nx.footer.setAssign called, key=%s", key);

            switch (key) {
                case "A":
                    citron_key_callbacks[0] = func;
                    break;
                case "B":
                    citron_key_callbacks[1] = func;
                    break;
                case "X":
                    citron_key_callbacks[2] = func;
                    break;
                case "Y":
                    citron_key_callbacks[3] = func;
                    break;
                case "L":
                    citron_key_callbacks[6] = func;
                    break;
                case "R":
                    citron_key_callbacks[7] = func;
                    break;
            }
        }

        setFixed(kind) {
            console.log("nx.footer.setFixed is not implemented, kind=%s", kind);
        }

        unsetAssign(key) {
            console.log("nx.footer.unsetAssign called, key=%s", key);

            switch (key) {
                case "A":
                    citron_key_callbacks[0] = function() {};
                    break;
                case "B":
                    citron_key_callbacks[1] = function() {};
                    break;
                case "X":
                    citron_key_callbacks[2] = function() {};
                    break;
                case "Y":
                    citron_key_callbacks[3] = function() {};
                    break;
                case "L":
                    citron_key_callbacks[6] = function() {};
                    break;
                case "R":
                    citron_key_callbacks[7] = function() {};
                    break;
            }
        }
    }

    class WindowNXPlayReport {
        incrementCounter(counter_id) {
            console.log("nx.playReport.incrementCounter is not implemented, counter_id=%d", counter_id);
        }

        setCounterSetIdentifier(counter_id) {
            console.log("nx.playReport.setCounterSetIdentifier is not implemented, counter_id=%d", counter_id);
        }
    }

    window.nx = new WindowNX();
    window.nx.footer = new WindowNXFooter();
    window.nx.playReport = new WindowNXPlayReport();

    // The native web child receives keyboard input without passing it through QWidget's
    // keyboard-to-NPad path. Translate only configured mappings so literal A/B/X/Y do not
    // conflict with keyboard-backed controller mappings such as left-stick A/W/S/D. Direction
    // mappings need arrow events for offline manuals, whose navigation lives on document.onkeydown.
    // Synthetic events from the controller input thread are excluded to avoid double activation.
    window.addEventListener("keydown", function(event) {
        if (!event.isTrusted || event.repeat) {
            return;
        }

        const configured = window.citron_host_key_bindings;
        const direction_key_code = configured && configured.directions
            ? configured.directions[event.keyCode] : undefined;
        if (direction_key_code !== undefined && direction_key_code !== event.keyCode) {
            const target = event.target;
            if (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement ||
                (target instanceof HTMLElement && target.isContentEditable)) {
                return;
            }
            const direction = {
                37: ["ArrowLeft", "ArrowLeft"],
                38: ["ArrowUp", "ArrowUp"],
                39: ["ArrowRight", "ArrowRight"],
                40: ["ArrowDown", "ArrowDown"],
            }[direction_key_code];
            if (direction) {
                event.preventDefault();
                event.stopImmediatePropagation();
                const mapped_event = new KeyboardEvent("keydown", {
                    key: direction[0], code: direction[1], bubbles: true, cancelable: true,
                });
                try {
                    Object.defineProperty(mapped_event, "keyCode", {value: direction_key_code});
                    Object.defineProperty(mapped_event, "which", {value: direction_key_code});
                } catch (_) {}
                (target || document).dispatchEvent(mapped_event);
                return;
            }
        }

        const mapped_index = configured && configured.buttons
            ? configured.buttons[event.keyCode] : undefined;
        const callback_index = mapped_index;
        if (callback_index === undefined) {
            return;
        }

        const callback = citron_key_callbacks[callback_index];
        if (callback != null) {
            event.preventDefault();
            callback();
        } else if (callback_index === 0 && document.activeElement &&
                   typeof document.activeElement.click === "function") {
            event.preventDefault();
            document.activeElement.click();
        }
    }, true);
})();
)";
