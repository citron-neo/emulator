// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from qt_web_browser_scripts.h. Shared verbatim across both native
// backends (WebKitGTK, WebView2) -- unlike WINDOW_NX_SCRIPT, none of these 4
// need any per-backend mechanism change, so they were byte-identical copies
// (differing only in char vs wchar_t storage type) until consolidated here.
// WebView2 widens these at the one call site that needs wchar_t
// (Utf8ToWide in webview2_web_browser.cpp) rather than storing a second copy.

#pragma once

constexpr char WEB_BROWSER_NX_FONT_CSS[] = R"(
(function() {
    function installCitronFonts() {
        if (document.getElementById('nx_font')) {
            return;
        }

        // WebView2's document-created injection runs before the parser is guaranteed to have
        // created <head> (or even <html>). Appending immediately used to throw, leaving every
        // Nintendo font unavailable and rendering the A/B/X/Y private-use glyphs as boxes.
        var target = document.head || document.documentElement;
        if (!target) {
            setTimeout(installCitronFonts, 0);
            return;
        }

        var css = document.createElement('style');
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

        target.appendChild(css);
    }

    installCitronFonts();
})();
)";

constexpr char WEB_BROWSER_LOAD_NX_FONT[] = R"(
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

constexpr char WEB_BROWSER_FOCUS_LINK_ELEMENT_SCRIPT[] = R"(
(function() {
    function invokeBack(endIfUnassigned) {
        var callbacks = window.citron_key_callbacks;
        if (callbacks && typeof callbacks[1] === 'function') {
            callbacks[1]();
        } else if (endIfUnassigned && window.nx && window.nx.endApplet) {
            window.nx.endApplet();
        // Native views start with an about:blank history entry. It is not an applet page, so Back
        // must exit rather than navigate to that blank view.
        } else if (window.history.length > 2) {
            window.history.back();
        } else if (window.nx && window.nx.endApplet) {
            window.nx.endApplet();
        }
    }

    function applyCompatibility() {
        // Arcropolis' header icons use the Nintendo WebApplet-only `ref` attribute
        // instead of standard HTML `src`. Translate it for desktop browser engines.
        document.querySelectorAll('img[ref]').forEach(function(image) {
            if (!image.getAttribute('src')) image.setAttribute('src', image.getAttribute('ref'));
        });

        // The mod-manager document can omit the visual back control even though its B footer
        // action exists. Recreate the same header element used by the other ARCropolis pages.
        var back = document.getElementById('ret-button');
        var isArcadia = !!document.querySelector(
            'script[src*="arcadia.js"], link[href*="arcadia.css"], #mods, #category_mover');
        var isArcropolis = isArcadia || !!document.querySelector(
            'script[src*="menu.js"], script[src*="configurator.js"], script[src*="workspaces.js"]');
        if (!back && isArcadia) {
            var header = document.querySelector('.header') || document.querySelector('#header > div');
            if (header) {
                back = document.createElement('a');
                back.id = 'ret-button';
                back.tabIndex = -1;
                back.className = 'header-decoration';
                back.setAttribute('nx-se-disabled', '');
                back.innerHTML = '<div class="ret-icon-wrapper">' +
                    '<img class="ret-icon-shadow" src="./help/img/icon/m_retnormal.svg">' +
                    '<img class="ret-icon" src="./help/img/icon/m_retnormal.svg"></div>';
                header.insertBefore(back, header.firstChild);
            }
        }

        // Every ARCropolis back control represents the currently assigned B action. Calling its
        // javascript: URL directly can navigate the outer page instead of completing WebSession.
        if (back && isArcropolis && !back.dataset.citronBackHandler) {
            back.dataset.citronBackHandler = '1';
            back.addEventListener('click', function(event) {
                event.preventDefault();
                event.stopImmediatePropagation();
                invokeBack(true);
            }, true);
        }

        // ARCadia's Switch helper normally replaces its placeholder with inline SVG after a
        // platform-only paint event. Its shared CSS hides the placeholder images, so install a
        // plain, uniquely-classed image that desktop engines can render without that lifecycle.
        if (back && isArcadia) {
            var wrapper = back.querySelector('.ret-icon-wrapper') || back;
            var icon = wrapper.querySelector('.citron-arcadia-back-icon');
            if (!icon) {
                wrapper.replaceChildren();
                icon = document.createElement('img');
                icon.className = 'citron-arcadia-back-icon';
                icon.src = './help/img/icon/m_retnormal.svg';
                icon.alt = '';
                wrapper.appendChild(icon);
            }
            icon.style.cssText =
                'display:block !important; visibility:visible !important; opacity:1 !important;' +
                'width:64px !important; height:64px !important; object-fit:contain !important;';
        }

        // The Switch-scale stroke overwhelms the orange fill after desktop browser zooming.
        var title = document.querySelector('.breadcrumb-list [data-msgid="textbox_id-10002"]');
        if (title) {
            title.style.color = '#ff4b00';
            title.style.webkitTextFillColor = '#ff4b00';
            title.style.webkitTextStrokeWidth = '0.5px';
        }
    }

    // ARCropolis builds its menu after the document event. Keep trying briefly so the
    // first item is keyboard/controller-focusable without requiring a mouse click.
    function focusFirst() {
        // Query buttons first instead of using one combined selector, which returns DOM order.
        // The header back-link precedes the menu and has tabindex=-1; selecting it broke every
        // Arcropolis handler that expects `.is-focused` to be a menu button.
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

    function visible(element) {
        var style = window.getComputedStyle(element);
        return style.display !== 'none' && style.visibility !== 'hidden' &&
               element.getClientRects().length !== 0;
    }

    function moveFocus(direction) {
        var items = Array.from(document.querySelectorAll(
            '.main button:not([disabled]), #mods > button:not([disabled]), button:not([disabled])'))
            .filter(visible);
        if (!items.length) return;

        var current = document.querySelector('.is-focused');
        var index = items.indexOf(current);
        if (index < 0) index = 0;
        else index = Math.max(0, Math.min(items.length - 1, index + direction));

        document.querySelectorAll('.is-focused').forEach(function(element) {
            if (element !== items[index]) element.classList.remove('is-focused');
        });
        items[index].classList.add('is-focused');
        items[index].focus();
        items[index].scrollIntoView({ block: 'nearest' });
    }

    // ARCropolis' page handlers omit preventDefault (and ARCadia only handles wraparound),
    // causing one key to move the browser scrollbar and sometimes the highlight in reverse.
    // Own vertical navigation in the capture phase only for those pages. Offline manuals have
    // their own document-level arrow handling and must receive the original event.
    if (!window.__citronNavigationHandler) {
        window.__citronNavigationHandler = true;
        window.addEventListener('keydown', function(event) {
            var isArcropolis = !!document.querySelector(
                'script[src*="arcadia.js"], link[href*="arcadia.css"], #mods, #category_mover, ' +
                'script[src*="menu.js"], script[src*="configurator.js"], script[src*="workspaces.js"]');
            if (!isArcropolis) return;
            var configured = window.citron_host_key_bindings;
            var mapped = configured && configured.directions
                ? configured.directions[event.keyCode] : undefined;
            var keyCode = mapped !== undefined ? mapped : event.keyCode;
            if (keyCode !== 37 && keyCode !== 38 && keyCode !== 39 && keyCode !== 40) return;
            var target = event.target;
            if (target && (target.isContentEditable ||
                           /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName))) return;
            event.preventDefault();
            event.stopImmediatePropagation();
            moveFocus(keyCode === 37 || keyCode === 38 ? -1 : 1);
        }, true);
    }
    applyCompatibility();
    focusFirst();
    var attempts = 0;
    var retry = setInterval(function() {
        applyCompatibility();
        if (!document.querySelector('.is-focused')) focusFirst();
        if (++attempts >= 100) clearInterval(retry);
    }, 50);
})();
)";

constexpr char WEB_BROWSER_GAMEPAD_SCRIPT[] = R"(
window.addEventListener("gamepadconnected", function(e) {
    console.log("Gamepad connected at index %d: %s. %d buttons, %d axes.",
        e.gamepad.index, e.gamepad.id, e.gamepad.buttons.length, e.gamepad.axes.length);
});

window.addEventListener("gamepaddisconnected", function(e) {
    console.log("Gamepad disconnected from index %d: %s", e.gamepad.index, e.gamepad.id);
});
)";
