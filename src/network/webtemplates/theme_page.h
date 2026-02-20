#pragma once

#include <QString>
#include "base_css.h"
#include "menu_css.h"
#include "menu_html.h"
#include "menu_js.h"
#include "theme_css.h"
#include "theme_html.h"
#include "theme_js.h"

inline QString generateThemePageHtml(const QString& deviceId = QString())
{
    QString html = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Theme Editor - Decenza DE1</title>
<style>
)HTML";

    html += WEB_CSS_VARIABLES;
    html += WEB_CSS_HEADER;
    html += WEB_CSS_MENU;
    html += WEB_CSS_THEME_EDITOR;
    html += WEB_CSS_THEME_ACTIONS;

    html += R"HTML(
</style>
</head>
<body>

<header class="header">
    <div class="header-content">
        <div style="display:flex;align-items:center;gap:1rem">
            <a href="/" class="back-btn">&larr;</a>
            <h1>Theme Editor</h1>
        </div>
        <div class="header-right">
            <span class="theme-name" id="themeName">Default</span>
)HTML";

    html += generateMenuHtml();

    html += R"HTML(
        </div>
    </div>
</header>
)HTML";

    html += WEB_HTML_THEME_BODY;

    html += R"HTML(
<script>
)HTML";

    // Embed device ID for ownership checks (community delete button)
    if (!deviceId.isEmpty()) {
        html += "const LOCAL_DEVICE_ID = '" + deviceId + "';\n";
    } else {
        html += "const LOCAL_DEVICE_ID = '';\n";
    }

    html += WEB_JS_MENU;
    html += WEB_JS_THEME_EDITOR;
    html += WEB_JS_THEME_LIBRARY;

    html += R"HTML(
</script>
</body>
</html>)HTML";

    return html;
}
