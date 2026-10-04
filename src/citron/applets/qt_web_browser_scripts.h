// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

constexpr char NX_FONT_CSS[] = R"(
(function() {
    css = document.createElement('style');
    css.type = 'text/css';
    css.id = 'nx_font';
    css.innerText = `
/* FontStandard */
@font-face {
    font-family: 'FontStandard';
    src: url('%1') format('truetype');
}

/* FontChineseSimplified */
@font-face {
    font-family: 'FontChineseSimplified';
    src: url('%2') format('truetype');
}

/* FontExtendedChineseSimplified */
@font-face {
    font-family: 'FontExtendedChineseSimplified';
    src: url('%3') format('truetype');
}

/* FontChineseTraditional */
@font-face {
    font-family: 'FontChineseTraditional';
    src: url('%4') format('truetype');
}

/* FontKorean */
@font-face {
    font-family: 'FontKorean';
    src: url('%5') format('truetype');
}

/* FontNintendoExtended */
@font-face {
    font-family: 'NintendoExt003';
    src: url('%6') format('truetype');
}

/* FontNintendoExtended2 */
@font-face {
    font-family: 'NintendoExt003';
    src: url('%7') format('truetype');
}
`;

    document.head.appendChild(css);
})();
)";

constexpr char LOAD_NX_FONT[] = R"(
(function() {
    var elements = document.querySelectorAll("*");

    for (var i = 0; i < elements.length; i++) {
        var style = window.getComputedStyle(elements[i], null);
        if (style.fontFamily.includes("Arial") || style.fontFamily.includes("Calibri") ||
            style.fontFamily.includes("Century") || style.fontFamily.includes("Times New Roman")) {
            elements[i].style.fontFamily = "FontStandard, FontChineseSimplified, FontExtendedChineseSimplified, FontChineseTraditional, FontKorean, NintendoExt003";
        } else {
            elements[i].style.fontFamily = style.fontFamily + ", FontStandard, FontChineseSimplified, FontExtendedChineseSimplified, FontChineseTraditional, FontKorean, NintendoExt003";
        }
    }
})();
)";

constexpr char FOCUS_LINK_ELEMENT_SCRIPT[] = R"(
(function() {
    // Arcropolis' header icons use the Nintendo WebApplet-only `ref` attribute
    // instead of standard HTML `src`. Translate it for Qt WebEngine.
    document.querySelectorAll('img[ref]').forEach(function(image) {
        if (!image.getAttribute('src')) image.setAttribute('src', image.getAttribute('ref'));
    });

    // Browser zoom otherwise scales the Switch-calibrated stroke until it obscures the fill.
    var title = document.querySelector('.breadcrumb-list [data-msgid="textbox_id-10002"]');
    if (title) {
        title.style.color = 'orangered';
        title.style.webkitTextStrokeWidth = '1px';
    }

    // Arcropolis creates menu controls after the document is ready. Retry briefly so
    // controller/keyboard fallback input has a focused target without a mouse click.
    function focusFirst() {
        var item = document.querySelector('.main button:not([disabled])') ||
                   document.querySelector('button:not([disabled])') ||
                   document.querySelector('input:not([disabled])') ||
                   document.querySelector('a[href]:not([tabindex="-1"])');
        if (item) {
            // Arcropolis' arrow handlers navigate from `.is-focused`, but register their
            // focus listener after document injection. Mark the initial item explicitly so
            // an early focus still has the same state as a user/controller focus event.
            if (!document.querySelector('.is-focused')) item.classList.add('is-focused');
            item.focus();
            return true;
        }
        return false;
    }
    if (focusFirst()) return;
    var attempts = 0;
    var retry = setInterval(function() {
        if (focusFirst() || ++attempts >= 100) clearInterval(retry);
    }, 50);
})();
)";

constexpr char GAMEPAD_SCRIPT[] = R"(
window.addEventListener("gamepadconnected", function(e) {
    console.log("Gamepad connected at index %d: %s. %d buttons, %d axes.",
        e.gamepad.index, e.gamepad.id, e.gamepad.buttons.length, e.gamepad.axes.length);
});

window.addEventListener("gamepaddisconnected", function(e) {
    console.log("Gamepad disconnected from index %d: %s", e.gamepad.index, e.gamepad.id);
});
)";

constexpr char WINDOW_NX_SCRIPT[] = R"(
var end_applet = false;
var citron_key_callbacks = [];
// Outgoing window.nx.sendMessage() calls are queued here and drained by the emulator on the
// GUI thread, which forwards each entry to the guest as interactive-out data. This is what
// lets pages such as ARCropolis's mod manager actually signal state changes and closure.
var citron_outgoing_messages = [];

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

            end_applet = true;
        }

        playSystemSe(system_se) {
            console.log("nx.playSystemSe is not implemented, system_se=%s", system_se);
        }

        sendMessage(message) {
            console.log("nx.sendMessage called, message=%s", message);

            citron_outgoing_messages.push(typeof message === "string" ? message : JSON.stringify(message));
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
})();
)";
