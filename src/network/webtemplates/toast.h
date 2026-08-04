#pragma once

// The bottom-centre transient message ("toast") shared by the Layout Editor
// (shotserver_layout.cpp) and the Theme Editor (theme_page.h).
//
// Style, markup and behaviour together, because the two pages each carried
// their own hand-written copy of all three and the copies had already drifted:
// five style values differed (bottom 1.5rem vs 20px, padding, font-size, #fff
// vs white, and a z-index of 9999 against 300), and only one of the two JS
// copies cleared its dismiss timer, so on the other a second toast inherited
// the first one's countdown and vanished early.

inline constexpr const char* WEB_CSS_TOAST = R"CSS(
.lib-toast {
    position: fixed;
    bottom: 1.5rem;
    left: 50%;
    transform: translateX(-50%);
    background: var(--accent);
    color: #fff;
    padding: 0.6rem 1.2rem;
    border-radius: 8px;
    font-size: 0.85rem;
    /* Where the toast sits in a page's stack is the page's call, so this is the
       one value a page may override — the Theme Editor sets --z-toast so the
       toast stays UNDER its full-screen CRT shader overlay (z 9999) and gets
       the effect drawn over it like everything else. */
    z-index: var(--z-toast, 9999);
    opacity: 0;
    transition: opacity 0.3s;
    pointer-events: none;
}
.lib-toast.show { opacity: 1; }
)CSS";

inline constexpr const char* WEB_HTML_TOAST = R"HTML(
<div class="lib-toast" id="libToast"></div>
)HTML";

// showToast(msg) holds for 2s; pass ms to hold longer.
inline constexpr const char* WEB_JS_TOAST = R"JS(
function showToast(msg, ms) {
    var t = document.getElementById('libToast');
    t.textContent = msg;
    t.classList.add('show');
    clearTimeout(window._toastTimer);
    window._toastTimer = setTimeout(function() { t.classList.remove('show'); }, ms || 2000);
}
)JS";
