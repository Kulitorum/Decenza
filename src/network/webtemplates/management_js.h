#pragma once

// Shared JavaScript utilities for the inventory management pages — /beans,
// /recipes, /equipment (polish-shotserver-inventory-pages). Consolidates the
// helpers that were copy-pasted verbatim across the three pages: HTML escaping,
// the status line, the fetch-JSON POST wrapper (unwraps {error}), a GET wrapper,
// and a dot-joiner mirroring the app's Theme.joinWithBullet (drops empty parts).
// Every fetch checks r.ok and carries error handling per SHOTSERVER.md.
inline constexpr const char* WEB_JS_MANAGEMENT = R"JS(
        const el = (id) => document.getElementById(id);
        const status = (m) => { const s = el('status'); if (s) s.textContent = m || ''; };
        const esc = (s) => String(s ?? '').replace(/[&<>"]/g,
            c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
        // Join non-empty parts with the app's bold middle dot.
        const bullet = (parts) => parts.filter(p => p !== null && p !== undefined
            && String(p).trim() !== '').join(' &middot; ');

        function getJson(url) {
            return fetch(url)
                .then(r => r.json().then(d => {
                    if (!r.ok || d.error) throw new Error(d.error || ('Server error (' + r.status + ')'));
                    return d;
                }));
        }
        function post(url, body) {
            return fetch(url, { method: 'POST', headers: {'Content-Type': 'application/json'},
                                body: JSON.stringify(body || {}) })
                .then(r => r.json().then(d => {
                    if (!r.ok || d.error) throw new Error(d.error || ('Server error (' + r.status + ')'));
                    return d;
                }));
        }
)JS";
