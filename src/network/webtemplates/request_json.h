#pragma once

// JSON request helper shared by the generated web pages. Needs WEB_JS_TOAST on
// the same page.
inline constexpr const char* WEB_JS_REQUEST_JSON = R"JS(
function reportScriptError(e) {
    console.error(e);
    showToast('Error: ' + ((e && e.message) || e || 'Unexpected error'), 3000);
}
// requestJson never rejects, but a throw in the caller's own .then handler or
// async function would, and would otherwise reach only the console.
window.addEventListener('unhandledrejection', function(ev) { reportScriptError(ev.reason); });

// Resolves to the parsed body, or to null after showing the failure as a toast.
// The toast carries the server's own reason: the "error" field of a JSON body,
// else a plain-text body, else the status (an HTML body, e.g. from a proxy, is
// not shown).
function requestJson(url, options) {
    var ctrl = new AbortController();
    var tid = setTimeout(function() { ctrl.abort(); }, 45000);
    var opts = Object.assign({}, options || {}, { signal: ctrl.signal });
    return fetch(url, opts).then(function(r) {
        clearTimeout(tid);
        if (r.ok) return r.json();
        return r.text().then(function(text) {
            var msg = text;
            try {
                var body = JSON.parse(text);
                if (body && body.error) msg = body.error;
            } catch (e) {}
            if (!msg || /^\s*</.test(msg)) msg = 'Server error (' + r.status + ')';
            throw new Error(msg);
        });
    }).catch(function(e) {
        clearTimeout(tid);
        showToast('Error: ' + (e.name === 'AbortError' ? 'Request timed out' : (e.message || 'Network error')), 3000);
        return null;
    });
}
)JS";
