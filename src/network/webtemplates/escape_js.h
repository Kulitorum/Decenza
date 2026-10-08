#pragma once

// The one HTML-escape helper every web page uses. It escapes both quote marks, so
// its result is safe in text and in an attribute delimited by either quote.
inline constexpr const char* WEB_JS_ESCAPE_HTML = R"JS(
        function escapeHtml(s) {
            if (s === null || s === undefined) return "";
            return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                            .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
        }
)JS";
