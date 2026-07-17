#pragma once

#include <QString>
#include "menu_html.h"

// Shared page chrome for the inventory management surfaces — /beans, /recipes,
// /equipment (polish-shotserver-inventory-pages). Emits the canonical
// `<header class="header">` with the ☕ Decenza logo (home / back to Shot
// History), the page title, and the shared burger menu — identical in structure
// to the Shot History page — so the three pages stop shipping a bare
// `<div class="header">` with a lone-emoji title. `titleHtml` may contain an
// HTML entity emoji prefix (e.g. "&#127793; Beans"); it is trusted caller text,
// never user input.
inline QString generateManagementHeader(const QString& titleHtml)
{
    QString html = R"HTML(<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="logo">&#9749; Decenza</a>
            <div class="header-right">
                <span class="page-title">)HTML";
    html += titleHtml;
    html += R"HTML(</span>)HTML";
    html += generateMenuHtml();
    html += R"HTML(
            </div>
        </div>
    </header>
)HTML";
    return html;
}
