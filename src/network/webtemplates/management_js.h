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

        // Read the body as text and parse it safely: a non-2xx response with a
        // non-JSON body (auth login page, proxy 502) must surface "Server error
        // (NNN)", not a misleading JSON parse error — while a JSON {error} body
        // still yields its message (SHOTSERVER.md fetch rules).
        function readJson(r) {
            return r.text().then(t => {
                let d = {};
                try { d = t ? JSON.parse(t) : {}; }
                catch (e) { if (!r.ok) throw new Error('Server error (' + r.status + ')'); throw e; }
                if (!r.ok || d.error) throw new Error(d.error || ('Server error (' + r.status + ')'));
                return d;
            });
        }
        function getJson(url, opts) { return fetch(url, opts).then(readJson); }
        function post(url, body) {
            return fetch(url, { method: 'POST', headers: {'Content-Type': 'application/json'},
                                body: JSON.stringify(body || {}) }).then(readJson);
        }
)JS";
