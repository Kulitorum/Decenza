#include "shotserver.h"
#include "webdebuglogger.h"
#include "webtemplates.h"
#include "../history/shothistorystorage.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../screensaver/screensavervideomanager.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../core/settingsserializer.h"
#include "../ai/aimanager.h"
#include "version.h"

#include <QNetworkInterface>
#include <QUdpSocket>
#include <QSet>
#include <QFile>
#include <QBuffer>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QUrl>
#include <QUrlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#ifndef Q_OS_IOS
#include <QProcess>
#endif
#include <QCoreApplication>
#include <QRegularExpression>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif

QString ShotServer::generateIndexPage() const
{
    return generateShotListPage();
}

QString ShotServer::generateShotListPage() const
{
    QVariantList shots = m_storage->getShots(0, 1000);  // Get more shots for filtering

    QString rows;
    for (const QVariant& v : std::as_const(shots)) {
        QVariantMap shot = v.toMap();

        int rating = qRound(shot["enjoyment"].toDouble());  // 0-100

        double ratio = 0;
        if (shot["doseWeight"].toDouble() > 0) {
            ratio = shot["finalWeight"].toDouble() / shot["doseWeight"].toDouble();
        }

        QString profileName = shot["profileName"].toString();
        QString beanBrand = shot["beanBrand"].toString();
        QString beanType = shot["beanType"].toString();
        QString dateTime = shot["dateTime"].toString();
        double doseWeight = shot["doseWeight"].toDouble();
        double finalWeight = shot["finalWeight"].toDouble();
        double duration = shot["duration"].toDouble();
        QString grinderSetting = shot["grinderSetting"].toString();
        double tempOverride = shot["temperatureOverride"].toDouble();  // Always has value
        double yieldOverride = shot["yieldOverride"].toDouble();  // Always has value

        // Escape for JavaScript string (single quotes) and HTML attribute
        auto escapeForJs = [](const QString& s) -> QString {
            QString escaped = s;
            escaped.replace("%", "%%");      // Must be first — protect QString::arg() placeholders
            escaped.replace("\\", "\\\\");
            escaped.replace("'", "\\'");
            escaped.replace("\"", "&quot;");
            escaped.replace("<", "&lt;");
            escaped.replace(">", "&gt;");
            return escaped;
        };

        QString profileJs = escapeForJs(profileName);
        QString brandJs = escapeForJs(beanBrand);
        QString coffeeJs = escapeForJs(beanType);
        QString profileHtml = profileName.toHtmlEscaped();
        QString brandHtml = beanBrand.toHtmlEscaped();
        QString coffeeHtml = beanType.toHtmlEscaped();

        // Build profile header: "Profile (Temp°C)"
        QString profileDisplay = profileHtml;
        if (tempOverride > 0) {
            profileDisplay += QString(" <span class=\"shot-temp\">(%1&deg;C)</span>")
                .arg(tempOverride, 0, 'f', 0);
        }

        // Build yield display: "Actual (Target) out" or just "Actual out"
        QString yieldDisplay;
        if (yieldOverride > 0 && qAbs(yieldOverride - finalWeight) > 0.5) {
            yieldDisplay = QString("<span class=\"metric-value\">%1g</span><span class=\"metric-target\">(%2g)</span>")
                .arg(finalWeight, 0, 'f', 1)
                .arg(yieldOverride, 0, 'f', 0);
        } else {
            yieldDisplay = QString("<span class=\"metric-value\">%1g</span>")
                .arg(finalWeight, 0, 'f', 1);
        }

        // Build bean display: "Brand Type (Grind)"
        QString beanDisplay;
        if (!beanBrand.isEmpty() || !beanType.isEmpty()) {
            beanDisplay = QString("<span class=\"clickable\" onclick=\"event.preventDefault(); event.stopPropagation(); setSearch('%1')\">%2</span>"
                                  "<span class=\"clickable\" onclick=\"event.preventDefault(); event.stopPropagation(); setSearch('%3')\">%4</span>")
                .arg(brandJs, brandHtml, coffeeJs, coffeeHtml);
            if (!grinderSetting.isEmpty()) {
                beanDisplay += QString(" <span class=\"shot-grind\">(%1)</span>")
                    .arg(grinderSetting.toHtmlEscaped());
            }
        }

        double drinkTds = shot["drinkTds"].toDouble();
        double drinkEy = shot["drinkEy"].toDouble();

        rows += QString(R"HTML(
            <div class="shot-card" onclick="toggleSelect(%1, this)" data-id="%1"
                 data-profile="%2" data-brand="%3" data-coffee="%4" data-rating="%5"
                 data-ratio="%6" data-duration="%7" data-date="%8" data-dose="%9" data-yield="%10"
                 data-tds="%15" data-ey="%16">
                <a href="/shot/%1" onclick="event.stopPropagation()" style="text-decoration:none;color:inherit;display:block;">
                    <div class="shot-header">
                        <span class="shot-profile clickable" onclick="event.preventDefault(); event.stopPropagation(); setSearch('%11')">%12</span>
                        <div class="shot-header-right">
                            <span class="shot-date">%8</span>
                            <input type="checkbox" class="shot-checkbox" data-id="%1" onclick="event.stopPropagation(); toggleSelect(%1, this.closest('.shot-card'))">
                        </div>
                    </div>
                    <div class="shot-metrics">
                        <div class="dose-group">
                            <div class="shot-metric">
                                <span class="metric-value">%9g</span>
                                <span class="metric-label">in</span>
                            </div>
                            <div class="shot-arrow">&#8594;</div>
                            <div class="shot-metric">
                                %13
                                <span class="metric-label">out</span>
                            </div>
                        </div>
                        <div class="shot-metric">
                            <span class="metric-value">1:%6</span>
                            <span class="metric-label">ratio</span>
                        </div>
                        <div class="shot-metric">
                            <span class="metric-value">%7s</span>
                            <span class="metric-label">time</span>
                        </div>
                    </div>
                    <div class="shot-footer">
                        <span class="shot-beans">%14</span>
                        <span class="shot-rating clickable" onclick="event.preventDefault(); event.stopPropagation(); setSearch('rating:%5+')">rating: %5</span>
                    </div>
                </a>
            </div>
        )HTML")
        .arg(shot["id"].toLongLong())       // %1
        .arg(profileHtml)                   // %2 (data attr, undecorated)
        .arg(brandHtml)                     // %3
        .arg(coffeeHtml)                    // %4
        .arg(rating)                        // %5
        .arg(ratio, 0, 'f', 1)              // %6
        .arg(duration, 0, 'f', 1)           // %7
        .arg(dateTime)                      // %8
        .arg(doseWeight, 0, 'f', 1)         // %9
        .arg(finalWeight, 0, 'f', 1)        // %10
        .arg(profileJs)                     // %11
        .arg(profileDisplay)                // %12 (profile with temp)
        .arg(yieldDisplay)                  // %13 (yield with target)
        .arg(beanDisplay)                   // %14 (beans with grind)
        .arg(drinkTds, 0, 'f', 2)           // %15
        .arg(drinkEy, 0, 'f', 2);           // %16
    }

    // Build HTML in chunks to avoid MSVC string literal size limit
    QString html;

    // Part 1: DOCTYPE and head start
    html += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Shot History - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --accent-dim: #a68a1f;
            --pressure: #18c37e;
            --flow: #4e85f4;
            --temp: #e73249;
            --weight: #a2693d;
            --weightFlow: #d4a574;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
            min-height: 100vh;
        }
)HTML";

    // Part 2: Header and layout CSS
    html += R"HTML(
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1200px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            justify-content: space-between;
        }
        .logo {
            font-size: 1.25rem;
            font-weight: 600;
            color: var(--accent);
            text-decoration: none;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .shot-count { color: var(--text-secondary); font-size: 0.875rem; }
        .container { max-width: 1200px; margin: 0 auto; padding: 1.5rem; }
        .shot-grid {
            display: grid;
            gap: 1rem;
            grid-template-columns: repeat(auto-fill, minmax(340px, 1fr));
        }
)HTML";

    // Part 3: Shot card CSS
    html += R"HTML(
        .shot-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 0.5rem 0.75rem;
            text-decoration: none;
            color: inherit;
            transition: background 0.2s ease, border-color 0.2s ease;
            display: block;
            content-visibility: auto;
            contain-intrinsic-size: auto 110px;
            position: relative;
        }
        .shot-card:hover { background: var(--surface-hover); border-color: var(--accent); }
        .shot-card.selected { border-color: var(--accent); }
        .shot-header { display: flex; justify-content: space-between; align-items: center; }
        .shot-header-right { display: flex; align-items: center; gap: 0.5rem; }
        .shot-profile { font-weight: 600; font-size: 1rem; color: var(--text); }
        .shot-date { font-size: 0.75rem; color: var(--text-secondary); white-space: nowrap; }
        .shot-metrics { display: flex; align-items: center; justify-content: space-between; }
        .dose-group {
            display: flex;
            align-items: center;
            gap: 0.3rem;
            padding: 0 0.3rem;
            border: 1px solid var(--border);
            border-radius: 4px;
        }
        .shot-metric { display: flex; flex-direction: column; align-items: center; }
        .shot-metric .metric-value { font-size: 1.125rem; font-weight: 600; color: var(--accent); }
        .shot-metric .metric-label { font-size: 0.625rem; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 0.05em; }
        .shot-arrow { color: var(--text-secondary); font-size: 1rem; }
        .shot-footer { display: flex; justify-content: space-between; align-items: center; }
        .shot-beans { font-size: 0.8125rem; color: var(--text-secondary); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 60%%; }
        .shot-rating { color: var(--accent); font-size: 0.875rem; }
        .shot-temp { color: var(--text-secondary); font-weight: normal; }
        .shot-grind { color: var(--text-secondary); font-weight: normal; }
        .metric-target { font-size: 0.75rem; color: var(--text-secondary); margin-left: 2px; }
        .empty-state { text-align: center; padding: 4rem 2rem; color: var(--text-secondary); }
        .empty-state h2 { margin-bottom: 0.5rem; color: var(--text); }
)HTML";

    // Part 4: Search and compare bar CSS
    html += R"HTML(
        .search-bar { display: flex; gap: 1rem; margin-bottom: 1.5rem; flex-wrap: wrap; align-items: center; }
        .search-help { font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 0.5rem; }
        .search-input {
            flex: 1;
            min-width: 200px;
            padding: 0.75rem 1rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            color: var(--text);
            font-size: 1rem;
        }
        .search-input:focus { outline: none; border-color: var(--accent); }
        .search-input::placeholder { color: var(--text-secondary); }
        .compare-bar {
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1rem 1.5rem;
            display: none;
            justify-content: center;
            align-items: center;
            gap: 1rem;
            z-index: 100;
        }
        .compare-bar.visible { display: flex; }
        .compare-btn {
            padding: 0.75rem 2rem;
            background: var(--accent);
            color: var(--bg);
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
        }
        .compare-btn:hover { opacity: 0.9; }
        .compare-btn:disabled { opacity: 0.4; cursor: default; }
        .delete-btn {
            padding: 0.75rem 1.5rem;
            background: #c0392b;
            color: #fff;
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
        }
        .delete-btn:hover { opacity: 0.9; }
        .clear-btn {
            padding: 0.75rem 1.5rem;
            background: transparent;
            color: var(--text-secondary);
            border: 1px solid var(--border);
            border-radius: 8px;
            cursor: pointer;
        }
)HTML";

    // Part 5: Checkbox and menu CSS
    html += R"HTML(
        .shot-checkbox {
            width: 24px;
            height: 24px;
            min-width: 24px;
            appearance: none;
            -webkit-appearance: none;
            background: var(--bg);
            border: 2px solid var(--border);
            border-radius: 4px;
            cursor: pointer;
            display: flex;
            justify-content: center;
            align-items: center;
        }
        .shot-checkbox:checked { background: var(--accent); border-color: var(--accent); }
        .shot-checkbox:checked::after { content: "✓"; color: var(--bg); font-size: 18px; font-weight: bold; line-height: 1; }
        .header-right { display: flex; align-items: center; gap: 1rem; }
        .menu-wrapper { position: relative; }
        .menu-btn {
            background: none;
            border: none;
            color: var(--text);
            font-size: 1.5rem;
            cursor: pointer;
            padding: 0.25rem 0.5rem;
            line-height: 1;
        }
        .menu-btn:hover { color: var(--accent); }
        .menu-dropdown {
            position: absolute;
            top: 100%%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            min-width: max-content;
            display: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 200;
        }
        .menu-dropdown.open { display: block; }
        .menu-item {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--text);
            text-decoration: none;
            border-bottom: 1px solid var(--border);
            white-space: nowrap;
        }
        .menu-item:last-child { border-bottom: none; }
        .menu-item:hover { background: var(--surface-hover); }
        .menu-item:first-child { border-radius: 7px 7px 0 0; }
        .menu-item:last-child { border-radius: 0 0 7px 7px; }
        .menu-item:only-child { border-radius: 7px; }
        .clickable { cursor: pointer; transition: color 0.2s; }
        .clickable:hover { color: var(--accent) !important; text-decoration: underline; }
)HTML";

    // Part 6: Sort dropdown CSS
    html += R"HTML(
        .sort-dir-btn { min-width: 2.2rem; padding-left: 0.5rem; padding-right: 0.5rem; text-align: center; }
        .sort-dropdown {
            position: absolute;
            top: 100%%;
            right: 0;
            margin-top: 0.25rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            box-shadow: 0 4px 16px rgba(0,0,0,0.4);
            min-width: 10rem;
            z-index: 100;
            visibility: hidden;
            opacity: 0;
            transition: opacity 0.15s;
        }
        .sort-dropdown.open { visibility: visible; opacity: 1; }
        .sort-option {
            padding: 0.5rem 0.75rem;
            cursor: pointer;
            font-size: 0.85rem;
            color: var(--text);
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .sort-option:first-child { border-radius: 8px 8px 0 0; }
        .sort-option:last-child { border-radius: 0 0 8px 8px; }
        .sort-option:hover { background: var(--surface-hover); }
        .sort-option .sort-check { display: none; color: var(--accent); margin-left: 0.5rem; }
        .sort-option.active .sort-check { display: inline; }
        .sort-anchor { position: relative; }
        .visible-count { font-size: 0.8rem; color: var(--text-secondary); margin-bottom: 0.5rem; }
)HTML";

    // Part 7: Search bar and panels CSS
    html += R"HTML(
        .search-row {
            display: flex;
            gap: 0.5rem;
            margin-bottom: 1rem;
            align-items: center;
        }
        .search-row .search-input {
            flex: 1;
            min-width: 0;
            padding: 0.6rem 0.75rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-size: 0.9rem;
        }
        .search-row .search-input:focus { outline: none; border-color: var(--accent); }
        .search-row .search-input::placeholder { color: var(--text-secondary); }
        .search-action-btn {
            padding: 0.6rem 0.75rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-size: 0.8rem;
            cursor: pointer;
            white-space: nowrap;
            transition: all 0.2s;
        }
        .search-action-btn:hover { border-color: var(--accent); color: var(--accent); }
        .search-action-btn:disabled { opacity: 0.4; cursor: default; }
        .search-action-btn:disabled:hover { border-color: var(--border); color: var(--text); }
        .search-panels-anchor { position: relative; z-index: 10; }
        .saved-searches-panel, .search-help-panel {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 0.75rem 1rem;
            position: absolute;
            left: 0;
            right: 0;
            top: 0;
            box-shadow: 0 4px 16px rgba(0,0,0,0.4);
            visibility: hidden;
            opacity: 0;
            transition: opacity 0.15s;
        }
        .saved-searches-panel.open, .search-help-panel.open { visibility: visible; opacity: 1; }
        .saved-search-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
        }
        .saved-search-item:last-child { border-bottom: none; }
        .saved-search-text {
            cursor: pointer;
            color: var(--text);
            flex: 1;
            font-size: 0.875rem;
        }
        .saved-search-text:hover { color: var(--accent); }
        .saved-search-delete {
            cursor: pointer;
            color: var(--text-secondary);
            font-size: 1.1rem;
            padding: 0 0.3rem;
            line-height: 1;
        }
        .saved-search-delete:hover { color: #c0392b; }
        .help-table {
            width: 100%%;
            border-collapse: collapse;
            font-size: 0.8rem;
        }
        .help-table th {
            text-align: left;
            padding: 0.4rem 0.6rem;
            border-bottom: 1px solid var(--border);
            color: var(--text-secondary);
            font-weight: 600;
        }
        .help-table td {
            padding: 0.4rem 0.6rem;
            border-bottom: 1px solid var(--border);
            color: var(--text);
        }
        .help-keyword {
            color: var(--accent);
            font-weight: 600;
            cursor: pointer;
            padding: 0.15rem 0.3rem;
            border-radius: 3px;
            transition: background 0.2s;
        }
        .help-keyword:hover { background: var(--surface-hover); }
        .help-syntax {
            font-family: monospace;
            background: var(--bg);
            padding: 0.15rem 0.4rem;
            border-radius: 3px;
            font-size: 0.8rem;
        }
        @media (max-width: 600px) {
            .shot-grid { grid-template-columns: 1fr; }
            .container { padding: 1rem; padding-bottom: 5rem; }
            .search-row { flex-wrap: wrap; }
            .sort-dropdown { right: auto; left: 0; }
        }
    </style>
</head>
)HTML";

    // Part 8: Body header with menu
    html += QString(R"HTML(<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="logo">&#9749; Decenza DE1</a>
            <div class="header-right">
                <span class="shot-count">%1 shots</span>)HTML").arg(m_storage->totalShots());

    html += generateMenuHtml(true);

    html += R"HTML(
            </div>
        </div>
    </header>
)HTML";

    // Part 9: Main content - search bar
    html += R"HTML(
    <main class="container">
        <div class="search-row">
            <input type="text" class="search-input" id="searchInput" placeholder="Search... (e.g. rating:70+ dose:16-18 ethiopia)" oninput="onSearchChange()">
            <button class="search-action-btn" id="keywordsBtn" onclick="toggleKeywords()">Keywords</button>
            <button class="search-action-btn" id="saveBtn" onclick="saveSearch()" disabled>Save</button>
            <button class="search-action-btn" id="savedBtn" onclick="toggleSavedSearches()" style="display:none;">&#9776; Saved</button>
            <span class="sort-anchor">
                <button class="search-action-btn sort-field-btn" id="sortFieldBtn" onclick="toggleSortMenu()">Date &#9662;</button>
                <div class="sort-dropdown" id="sortDropdown">
                    <div class="sort-option active" data-sort="date" data-default-dir="desc" onclick="selectSort('date')">Date <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="profile" data-default-dir="asc" onclick="selectSort('profile')">Profile <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="brand" data-default-dir="asc" onclick="selectSort('brand')">Roaster <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="coffee" data-default-dir="asc" onclick="selectSort('coffee')">Coffee <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="rating" data-default-dir="desc" onclick="selectSort('rating')">Rating <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="ratio" data-default-dir="desc" onclick="selectSort('ratio')">Ratio <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="duration" data-default-dir="asc" onclick="selectSort('duration')">Duration <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="dose" data-default-dir="desc" onclick="selectSort('dose')">Dose <span class="sort-check">&#10003;</span></div>
                    <div class="sort-option" data-sort="yield" data-default-dir="desc" onclick="selectSort('yield')">Yield <span class="sort-check">&#10003;</span></div>
                </div>
                <button class="search-action-btn sort-dir-btn" id="sortDirBtn" onclick="toggleSortDir()">&#9660;</button>
            </span>
        </div>
        <div class="search-panels-anchor">
        <div class="saved-searches-panel" id="savedPanel">
            <div id="savedList"></div>
        </div>
        <div class="search-help-panel" id="helpPanel">
            <p style="margin-bottom:0.6rem;font-size:0.85rem;color:var(--text-secondary);">Use keywords to filter by numeric fields. Click a keyword to add it to your search.</p>
            <table class="help-table">
                <tr><th>Keyword</th><th>Filters</th><th>Example</th></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('rating:')">rating:</span></td><td>Enjoyment (0-100)</td><td><span class="help-syntax">rating:70+</span></td></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('dose:')">dose:</span></td><td>Dose weight (g)</td><td><span class="help-syntax">dose:16-18</span></td></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('yield:')">yield:</span></td><td>Yield weight (g)</td><td><span class="help-syntax">yield:30-40</span></td></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('time:')">time:</span></td><td>Duration (seconds)</td><td><span class="help-syntax">time:25-35</span></td></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('tds:')">tds:</span></td><td>TDS</td><td><span class="help-syntax">tds:1.3-1.5</span></td></tr>
                <tr><td><span class="help-keyword" onclick="insertSearchKeyword('ey:')">ey:</span></td><td>Extraction yield (%%)</td><td><span class="help-syntax">ey:18-22</span></td></tr>
            </table>
            <p style="margin-top:0.5rem;font-size:0.75rem;color:var(--text-secondary);">Syntax: <span class="help-syntax">N</span> (exact), <span class="help-syntax">N-M</span> (range), <span class="help-syntax">N+</span> (minimum)<br>Combine keywords with text: <span class="help-syntax">ethiopia dose:18 rating:70+</span></p>
        </div>
        </div>
)HTML";

    // Part 10: Grid
    html += QString(R"HTML(
        <div class="visible-count" id="visibleCount">Showing %1 shots</div>
        <div class="shot-grid" id="shotGrid">
            %2
        </div>
    </main>
    <div class="compare-bar" id="compareBar">
        <span id="selectedCount">0 selected</span>
        <button class="compare-btn" id="compareBtn" onclick="compareSelected()" disabled>Compare Shots</button>
        <button class="delete-btn" onclick="deleteSelected()">Delete</button>
        <button class="clear-btn" onclick="clearSelection()">Clear</button>
    </div>
)HTML").arg(m_storage->totalShots())
      .arg(rows.isEmpty() ? "<div class='empty-state'><h2>No shots yet</h2><p>Pull some espresso to see your history here</p></div>" : rows);

    // Part 11: Script - selection functions
    html += R"HTML(
    <script>
        var selectedShots = [];
        var currentSort = { field: 'date', dir: 'desc' };
        var savedSearches = [];

        function toggleSelect(id, card) {
            var idx = selectedShots.indexOf(id);
            if (idx >= 0) {
                selectedShots.splice(idx, 1);
                card.classList.remove("selected");
            } else {
                if (selectedShots.length < 5) {
                    selectedShots.push(id);
                    card.classList.add("selected");
                }
            }
            updateCompareBar();
        }

        function updateCompareBar() {
            var bar = document.getElementById("compareBar");
            var count = document.getElementById("selectedCount");
            var compareBtn = document.getElementById("compareBtn");
            if (selectedShots.length >= 1) {
                bar.classList.add("visible");
                count.textContent = selectedShots.length + " selected";
                compareBtn.disabled = selectedShots.length < 2;
            } else {
                bar.classList.remove("visible");
            }
            document.querySelectorAll(".shot-checkbox").forEach(function(cb) {
                cb.checked = selectedShots.indexOf(parseInt(cb.dataset.id)) >= 0;
            });
        }

        function clearSelection() {
            selectedShots = [];
            document.querySelectorAll(".shot-card").forEach(function(c) { c.classList.remove("selected"); });
            updateCompareBar();
        }

        function compareSelected() {
            if (selectedShots.length >= 2) {
                window.location.href = "/compare/" + selectedShots.join(",");
            }
        }

        function deleteSelected() {
            if (selectedShots.length === 0) return;
            var n = selectedShots.length;
            if (!confirm("Delete " + n + " shot" + (n > 1 ? "s" : "") + "? This cannot be undone.")) return;
            fetch("/api/shots/delete", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ ids: selectedShots })
            }).then(function(resp) { return resp.json(); })
              .then(function(data) {
                if (data.deleted > 0) {
                    window.location.reload();
                } else {
                    alert("Failed to delete shots.");
                }
            }).catch(function() { alert("Failed to delete shots."); });
        }

)HTML";

    // Part 12: Script - search parsing (port of QML buildFilter)
    html += R"HTML(
        function parseSearchKeywords(text) {
            var filters = {};
            var searchText = text;
            var keywords = [
                { pattern: /\brating:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minRating", maxKey: "maxRating" },
                { pattern: /\brating:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minRating", maxKey: null },
                { pattern: /\brating:(\d+(?:\.\d+)?)\b/g, minKey: "minRating", maxKey: "maxRating", exact: true },
                { pattern: /\bdose:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minDose", maxKey: "maxDose" },
                { pattern: /\bdose:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minDose", maxKey: null },
                { pattern: /\bdose:(\d+(?:\.\d+)?)\b/g, minKey: "minDose", maxKey: "maxDose", exact: true },
                { pattern: /\byield:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minYield", maxKey: "maxYield" },
                { pattern: /\byield:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minYield", maxKey: null },
                { pattern: /\byield:(\d+(?:\.\d+)?)\b/g, minKey: "minYield", maxKey: "maxYield", exact: true },
                { pattern: /\btime:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minDuration", maxKey: "maxDuration" },
                { pattern: /\btime:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minDuration", maxKey: null },
                { pattern: /\btime:(\d+(?:\.\d+)?)\b/g, minKey: "minDuration", maxKey: "maxDuration", exact: true },
                { pattern: /\btds:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minTds", maxKey: "maxTds" },
                { pattern: /\btds:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minTds", maxKey: null },
                { pattern: /\btds:(\d+(?:\.\d+)?)\b/g, minKey: "minTds", maxKey: "maxTds", exact: true },
                { pattern: /\bey:(\d+(?:\.\d+)?)-(\d+(?:\.\d+)?)\b/g, minKey: "minEy", maxKey: "maxEy" },
                { pattern: /\bey:(\d+(?:\.\d+)?)\+(?=\s|$)/g, minKey: "minEy", maxKey: null },
                { pattern: /\bey:(\d+(?:\.\d+)?)\b/g, minKey: "minEy", maxKey: "maxEy", exact: true }
            ];
            for (var i = 0; i < keywords.length; i++) {
                var kw = keywords[i];
                var match = kw.pattern.exec(searchText);
                if (match) {
                    if (match.length === 3) {
                        filters[kw.minKey] = parseFloat(match[1]);
                        filters[kw.maxKey] = parseFloat(match[2]);
                    } else if (kw.exact) {
                        filters[kw.minKey] = parseFloat(match[1]);
                        filters[kw.maxKey] = parseFloat(match[1]);
                    } else {
                        filters[kw.minKey] = parseFloat(match[1]);
                    }
                    searchText = searchText.replace(match[0], "");
                }
            }
            // Strip remaining keyword tokens
            searchText = searchText.replace(/\b(rating|dose|yield|time|tds|ey):\d+(?:\.\d+)?(?:-\d+(?:\.\d+)?|\+)?/g, "");
            searchText = searchText.trim().replace(/\s+/g, " ");
            return { filters: filters, remaining: searchText };
        }

        function onSearchChange() {
            var input = document.getElementById('searchInput');
            var text = input.value;
            var parsed = parseSearchKeywords(text);
            var f = parsed.filters;
            var remaining = parsed.remaining.toLowerCase();

            var cards = Array.from(document.querySelectorAll('.shot-card'));
            var visibleCount = 0;
            cards.forEach(function(card) {
                var show = true;
                // Numeric keyword filters
                if (f.minRating !== undefined) { if (parseFloat(card.dataset.rating) < f.minRating) show = false; }
                if (f.maxRating !== undefined) { if (parseFloat(card.dataset.rating) > f.maxRating) show = false; }
                if (f.minDose !== undefined) { if (parseFloat(card.dataset.dose) < f.minDose) show = false; }
                if (f.maxDose !== undefined) { if (parseFloat(card.dataset.dose) > f.maxDose) show = false; }
                if (f.minYield !== undefined) { if (parseFloat(card.dataset.yield) < f.minYield) show = false; }
                if (f.maxYield !== undefined) { if (parseFloat(card.dataset.yield) > f.maxYield) show = false; }
                if (f.minDuration !== undefined) { if (parseFloat(card.dataset.duration) < f.minDuration) show = false; }
                if (f.maxDuration !== undefined) { if (parseFloat(card.dataset.duration) > f.maxDuration) show = false; }
                if (f.minTds !== undefined) { if (parseFloat(card.dataset.tds) < f.minTds) show = false; }
                if (f.maxTds !== undefined) { if (parseFloat(card.dataset.tds) > f.maxTds) show = false; }
                if (f.minEy !== undefined) { if (parseFloat(card.dataset.ey) < f.minEy) show = false; }
                if (f.maxEy !== undefined) { if (parseFloat(card.dataset.ey) > f.maxEy) show = false; }
                // Text search: split into words (AND logic, matching app behavior)
                if (remaining) {
                    var searchWords = remaining.replace(/[\-\/.]/g, ' ').split(/\s+/);
                    var cardText = card.textContent.toLowerCase();
                    for (var w = 0; w < searchWords.length; w++) {
                        if (searchWords[w] && !cardText.includes(searchWords[w])) { show = false; break; }
                    }
                }
                card.style.display = show ? '' : 'none';
                if (show) visibleCount++;
            });
            sortVisibleCards();
            document.getElementById('visibleCount').textContent = 'Showing ' + visibleCount + ' shots';
            updateSaveButton();
        }

        function sortVisibleCards() {
            var grid = document.getElementById('shotGrid');
            var cards = Array.from(document.querySelectorAll('.shot-card'));
            var visibleCards = cards.filter(function(c) { return c.style.display !== 'none'; });
            visibleCards.sort(function(a, b) {
                var aVal, bVal;
                var field = currentSort.field;
                var dir = currentSort.dir === 'asc' ? 1 : -1;
                if (field === 'date') { aVal = a.dataset.date || ''; bVal = b.dataset.date || ''; return dir * aVal.localeCompare(bVal); }
                else if (field === 'profile') { aVal = (a.dataset.profile || '').toLowerCase(); bVal = (b.dataset.profile || '').toLowerCase(); return dir * aVal.localeCompare(bVal); }
                else if (field === 'brand') { aVal = (a.dataset.brand || '').toLowerCase(); bVal = (b.dataset.brand || '').toLowerCase(); return dir * aVal.localeCompare(bVal); }
                else if (field === 'coffee') { aVal = (a.dataset.coffee || '').toLowerCase(); bVal = (b.dataset.coffee || '').toLowerCase(); return dir * aVal.localeCompare(bVal); }
                else if (field === 'rating') { aVal = parseFloat(a.dataset.rating) || 0; bVal = parseFloat(b.dataset.rating) || 0; return dir * (aVal - bVal); }
                else if (field === 'ratio') { aVal = parseFloat(a.dataset.ratio) || 0; bVal = parseFloat(b.dataset.ratio) || 0; return dir * (aVal - bVal); }
                else if (field === 'duration') { aVal = parseFloat(a.dataset.duration) || 0; bVal = parseFloat(b.dataset.duration) || 0; return dir * (aVal - bVal); }
                else if (field === 'dose') { aVal = parseFloat(a.dataset.dose) || 0; bVal = parseFloat(b.dataset.dose) || 0; return dir * (aVal - bVal); }
                else if (field === 'yield') { aVal = parseFloat(a.dataset.yield) || 0; bVal = parseFloat(b.dataset.yield) || 0; return dir * (aVal - bVal); }
                return 0;
            });
            visibleCards.forEach(function(card) { grid.appendChild(card); });
        }

        function setSearch(text) {
            document.getElementById('searchInput').value = text;
            onSearchChange();
        }
)HTML";

    // Part 13: Script - saved searches
    html += R"HTML(
        function escapeHtml(str) {
            var div = document.createElement('div');
            div.appendChild(document.createTextNode(str));
            return div.innerHTML;
        }

        function loadSavedSearches() {
            fetch('/api/saved-searches')
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    savedSearches = data.searches || [];
                    updateSavedUI();
                })
                .catch(function() {});
        }

        function saveSearch() {
            var text = document.getElementById('searchInput').value.trim();
            if (!text) return;
            if (savedSearches.indexOf(text) >= 0) return;
            fetch('/api/saved-searches', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ search: text })
            }).then(function(r) { return r.json(); })
              .then(function(data) {
                if (data.success) {
                    savedSearches.push(text);
                    updateSavedUI();
                    updateSaveButton();
                }
            }).catch(function() {});
        }

        function deleteSavedSearch(text) {
            fetch('/api/saved-searches', {
                method: 'DELETE',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ search: text })
            }).then(function(r) { return r.json(); })
              .then(function(data) {
                if (data.success) {
                    var idx = savedSearches.indexOf(text);
                    if (idx >= 0) savedSearches.splice(idx, 1);
                    updateSavedUI();
                    updateSaveButton();
                }
            }).catch(function() {});
        }

        function applySavedSearch(text) {
            document.getElementById('searchInput').value = text;
            document.getElementById('savedPanel').classList.remove('open');
            onSearchChange();
        }

        function updateSavedUI() {
            var list = document.getElementById('savedList');
            var btn = document.getElementById('savedBtn');
            while (list.firstChild) list.removeChild(list.firstChild);
            if (savedSearches.length === 0) {
                btn.style.display = 'none';
                document.getElementById('savedPanel').classList.remove('open');
                return;
            }
            btn.style.display = '';
            for (var i = 0; i < savedSearches.length; i++) {
                var item = document.createElement('div');
                item.className = 'saved-search-item';
                var textSpan = document.createElement('span');
                textSpan.className = 'saved-search-text';
                textSpan.textContent = savedSearches[i];
                textSpan.addEventListener('click', (function(s) {
                    return function() { applySavedSearch(s); };
                })(savedSearches[i]));
                var delSpan = document.createElement('span');
                delSpan.className = 'saved-search-delete';
                delSpan.textContent = '\u00d7';
                delSpan.addEventListener('click', (function(s) {
                    return function() { deleteSavedSearch(s); };
                })(savedSearches[i]));
                item.appendChild(textSpan);
                item.appendChild(delSpan);
                list.appendChild(item);
            }
        }

        function updateSaveButton() {
            var btn = document.getElementById('saveBtn');
            var text = document.getElementById('searchInput').value.trim();
            btn.disabled = !text || savedSearches.indexOf(text) >= 0;
        }

        function toggleSavedSearches() {
            document.getElementById('savedPanel').classList.toggle('open');
            document.getElementById('helpPanel').classList.remove('open');
        }

        function toggleKeywords() {
            document.getElementById('helpPanel').classList.toggle('open');
            document.getElementById('savedPanel').classList.remove('open');
        }

        function insertSearchKeyword(keyword) {
            var input = document.getElementById('searchInput');
            var text = input.value;
            if (text.length > 0 && !text.endsWith(' ')) {
                text += ' ';
            }
            input.value = text + keyword;
            document.getElementById('helpPanel').classList.remove('open');
            input.focus();
        }
)HTML";

    // Part 14: Script - sort and menu functions
    html += R"HTML(
        var sortLabels = { date: "Date", profile: "Profile", brand: "Roaster", coffee: "Coffee", rating: "Rating", ratio: "Ratio", duration: "Duration", dose: "Dose", yield: "Yield" };

        function toggleSortMenu() {
            document.getElementById("sortDropdown").classList.toggle("open");
        }

        function selectSort(field) {
            var opts = document.querySelectorAll('.sort-option');
            var defaultDir = 'desc';
            opts.forEach(function(opt) {
                if (opt.dataset.sort === field) {
                    opt.classList.add('active');
                    defaultDir = opt.dataset.defaultDir;
                } else {
                    opt.classList.remove('active');
                }
            });
            currentSort.field = field;
            currentSort.dir = defaultDir;
            document.getElementById("sortFieldBtn").innerHTML = sortLabels[field] + " &#9662;";
            document.getElementById("sortDirBtn").innerHTML = defaultDir === 'asc' ? "&#9650;" : "&#9660;";
            document.getElementById("sortDropdown").classList.remove("open");
            onSearchChange();
        }

        function toggleSortDir() {
            currentSort.dir = currentSort.dir === 'asc' ? 'desc' : 'asc';
            document.getElementById("sortDirBtn").innerHTML = currentSort.dir === 'asc' ? "&#9650;" : "&#9660;";
            onSearchChange();
        }

        function toggleMenu() {
            document.getElementById("menuDropdown").classList.toggle("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            if (!e.target.closest(".menu-btn") && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
            var sortDrop = document.getElementById("sortDropdown");
            if (!e.target.closest(".sort-anchor") && sortDrop.classList.contains("open")) {
                sortDrop.classList.remove("open");
            }
        });
)HTML";

    // Part 15: Script - power functions and init
    html += R"HTML(
        var powerState = {awake: false, state: "Unknown"};

        function updatePowerButton() {
            var btn = document.getElementById("powerToggle");
            if (powerState.state === "Unknown" || !powerState.connected) {
                btn.innerHTML = "&#128268; Disconnected";
            } else if (powerState.awake) {
                btn.innerHTML = "&#128164; Put to Sleep";
            } else {
                btn.innerHTML = "&#9889; Wake Up";
            }
        }

        function fetchPowerState() {
            fetch("/api/power/status")
                .then(function(r) { return r.json(); })
                .then(function(data) { powerState = data; updatePowerButton(); })
                .catch(function() {});
        }

        function togglePower() {
            var action = powerState.awake ? "sleep" : "wake";
            fetch("/api/power/" + action)
                .then(function(r) { return r.json(); })
                .then(function() { setTimeout(fetchPowerState, 1000); });
        }

        fetchPowerState();
        loadSavedSearches();
        var pwrTimer = setInterval(fetchPowerState, 5000);
        document.addEventListener('visibilitychange', function() {
            if (document.hidden) { clearInterval(pwrTimer); }
            else { fetchPowerState(); pwrTimer = setInterval(fetchPowerState, 5000); }
        });
    </script>
</body>
</html>
)HTML";

    return html;
}

QString ShotServer::generateShotDetailPage(qint64 shotId) const
{
    QVariantMap shot = m_storage->getShot(shotId);
    if (shot.isEmpty()) {
        return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Not Found</title></head>"
                  "<body style=\"background:#0d1117;color:#fff;font-family:sans-serif;padding:2rem;\">"
                  "<h1>Shot not found</h1><a href=\"/\" style=\"color:#c9a227;\">Back to list</a></body></html>");
    }

    double ratio = 0;
    if (shot["doseWeight"].toDouble() > 0) {
        ratio = shot["finalWeight"].toDouble() / shot["doseWeight"].toDouble();
    }

    int rating = qRound(shot["enjoyment"].toDouble() / 20.0);
    QString stars;
    for (int i = 0; i < 5; i++) {
        stars += (i < rating) ? "&#9733;" : "&#9734;";
    }

    // Escape for embedding in JavaScript string literals (inside double quotes)
    auto jsEscape = [](const QString& s) -> QString {
        QString r = s;
        r.replace(QLatin1String("%"), QLatin1String("%%"));    // Must be first: prevent %1-%99 arg placeholders
        r.replace(QLatin1String("\\"), QLatin1String("\\\\"));
        r.replace(QLatin1String("\""), QLatin1String("\\\""));
        r.replace(QLatin1String("\n"), QLatin1String("\\n"));
        r.replace(QLatin1String("\r"), QLatin1String(""));
        r.replace(QLatin1String("<"), QLatin1String("\\u003c")); // Prevent script tag breakout
        return r;
    };

    // Temperature and yield overrides (always have values)
    double tempOverride = shot["temperatureOverride"].toDouble();
    double yieldOverride = shot["yieldOverride"].toDouble();
    double finalWeight = shot["finalWeight"].toDouble();

    // Build yield display with optional target
    QString yieldDisplay = QString("%1g").arg(finalWeight, 0, 'f', 1);
    if (yieldOverride > 0 && qAbs(yieldOverride - finalWeight) > 0.5) {
        yieldDisplay += QString(" <span class=\"target\">(%1g)</span>").arg(yieldOverride, 0, 'f', 0);
    }

    // Convert time-series data to JSON arrays for Chart.js
    auto pointsToJson = [](const QVariantList& points) -> QString {
        QStringList items;
        for (const QVariant& p : points) {
            QVariantMap pt = p.toMap();
            items << QString("{x:%1,y:%2}").arg(pt["x"].toDouble(), 0, 'f', 2).arg(pt["y"].toDouble(), 0, 'f', 2);
        }
        return "[" + items.join(",") + "]";
    };

    // Convert goal data with nulls at gaps (where time jumps > 0.5s)
    auto goalPointsToJson = [](const QVariantList& points) -> QString {
        QStringList items;
        double lastX = -999;
        for (const QVariant& p : points) {
            QVariantMap pt = p.toMap();
            double x = pt["x"].toDouble();
            double y = pt["y"].toDouble();
            // Insert null to break line if there's a gap > 0.5 seconds
            if (lastX >= 0 && (x - lastX) > 0.5) {
                items << QString("{x:%1,y:null}").arg((lastX + x) / 2, 0, 'f', 2);
            }
            items << QString("{x:%1,y:%2}").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2);
            lastX = x;
        }
        return "[" + items.join(",") + "]";
    };

    QString pressureData = pointsToJson(shot["pressure"].toList());
    QString flowData = pointsToJson(shot["flow"].toList());
    QString tempData = pointsToJson(shot["temperature"].toList());
    QString weightData = pointsToJson(shot["weight"].toList());
    QString weightFlowRateData = pointsToJson(shot["weightFlowRate"].toList());
    QString pressureGoalData = goalPointsToJson(shot["pressureGoal"].toList());
    QString flowGoalData = goalPointsToJson(shot["flowGoal"].toList());

    // Convert phase markers to JSON for Chart.js
    auto phasesToJson = [](const QVariantList& phases) -> QString {
        QStringList items;
        for (const QVariant& p : phases) {
            QVariantMap phase = p.toMap();
            QString label = phase["label"].toString();
            if (label == "Start") continue;  // Skip start marker
            // Escape both label and reason for safe embedding in JS string literals
            auto jsStringEscape = [](QString s) -> QString {
                s.replace(QLatin1String("\\"), QLatin1String("\\\\"));
                s.replace(QLatin1String("\""), QLatin1String("\\\""));
                s.replace(QLatin1String("\n"), QLatin1String("\\n"));
                s.replace(QLatin1String("\r"), QLatin1String(""));
                s.replace(QLatin1String("<"), QLatin1String("\\u003c"));
                return s;
            };
            QString reason = jsStringEscape(phase["transitionReason"].toString());
            label = jsStringEscape(label);
            items << QString("{time:%1,label:\"%2\",reason:\"%3\"}")
                .arg(phase["time"].toDouble(), 0, 'f', 2)
                .arg(label)
                .arg(reason);
        }
        return "[" + items.join(",") + "]";
    };
    QString phaseData = phasesToJson(shot["phases"].toList());

    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>%1 - Decenza DE1</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>)HTML" R"HTML(
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --pressure: #18c37e;
            --flow: #4e85f4;
            --temp: #e73249;
            --weight: #a2693d;
            --weightFlow: #d4a574;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1400px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
            line-height: 1;
            padding: 0.25rem;
        }
        .back-btn:hover { color: var(--accent); }
        .header-title {
            flex: 1;
        }
        .header-title h1 {
            font-size: 1.125rem;
            font-weight: 600;
        }
        .header-title .subtitle {
            font-size: 0.75rem;
            color: var(--text-secondary);
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1.5rem;
        }
        .metrics-bar {
            display: flex;
            gap: 1rem;
            flex-wrap: wrap;
            margin-bottom: 1.5rem;
        }
        .metric-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 1rem 1.25rem;
            min-width: 100px;
            text-align: center;
        }
        .metric-card .value {
            font-size: 1.5rem;
            font-weight: 700;
            color: var(--accent);
        }
        .metric-card .value .target {
            font-size: 0.875rem;
            font-weight: 400;
            color: var(--text-secondary);
        }
        .metric-card .label {
            font-size: 0.6875rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .chart-container {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1.5rem;
        }
        .chart-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 1rem;
            flex-wrap: wrap;
            gap: 0.5rem;
        }
        .chart-title {
            font-size: 1rem;
            font-weight: 600;
        }
        .chart-toggles {
            display: flex;
            gap: 0.5rem;
            flex-wrap: wrap;
        }
        .toggle-btn {
            padding: 0.375rem 0.75rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text-secondary);
            font-size: 0.75rem;
            cursor: pointer;
            transition: all 0.15s ease;
            display: flex;
            align-items: center;
            gap: 0.375rem;
        }
        .toggle-btn:hover { border-color: var(--text-secondary); }
        .toggle-btn.active { background: var(--surface-hover); color: var(--text); }
        .toggle-btn .dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
        }
        .toggle-btn.pressure .dot { background: var(--pressure); }
        .toggle-btn.flow .dot { background: var(--flow); }
        .toggle-btn.temp .dot { background: var(--temp); }
        .toggle-btn.weight .dot { background: var(--weight); }
        .toggle-btn.weightFlow .dot { background: var(--weightFlow); }
        .chart-wrapper {
            position: relative;
            height: 400px;
        }
        .info-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 1rem;
        }
        .info-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.25rem;
        }
        .info-card h3 {
            font-size: 0.875rem;
            font-weight: 600;
            margin-bottom: 0.75rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .info-row {
            display: flex;
            justify-content: space-between;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
        }
        .info-row:last-child { border-bottom: none; }
        .info-row .label { color: var(--text-secondary); }
        .info-row .value { font-weight: 500; }
        .notes-text {
            color: var(--text-secondary);
            font-style: italic;
        }
        .rating { color: var(--accent); font-size: 1.125rem; }
        .edit-btn {
            background: none;
            border: 1px solid var(--border);
            color: var(--text-secondary);
            font-size: 0.875rem;
            cursor: pointer;
            padding: 0.375rem 0.75rem;
            border-radius: 6px;
            white-space: nowrap;
        }
        .edit-btn:hover { color: var(--accent); border-color: var(--accent); }
        .edit-bar {
            position: fixed;
            bottom: 0;
            left: 0;
            right: 0;
            background: var(--surface);
            border-top: 1px solid var(--border);
            padding: 1rem 1.5rem;
            display: none;
            justify-content: center;
            gap: 1rem;
            z-index: 200;
        }
        .edit-bar.visible { display: flex; }
        .edit-bar button {
            padding: 0.75rem 2rem;
            border: none;
            border-radius: 8px;
            font-size: 0.9375rem;
            font-weight: 600;
            cursor: pointer;
        }
        .save-btn { background: var(--accent); color: #000; }
        .save-btn:hover { opacity: 0.9; }
        .cancel-btn { background: var(--surface-hover); color: var(--text); border: 1px solid var(--border) !important; }
        .cancel-btn:hover { border-color: var(--text-secondary) !important; }
        .edit-input, .edit-select, .edit-textarea {
            width: 100%;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-family: inherit;
            font-size: 0.875rem;
            padding: 0.5rem 0.75rem;
        }
        .edit-input:focus, .edit-select:focus, .edit-textarea:focus {
            outline: none;
            border-color: var(--accent);
        }
        .edit-select { cursor: pointer; }
        .edit-select option { background: var(--surface); color: var(--text); }
        .edit-textarea { min-height: 15em; resize: vertical; }
        .notes-card-edit { grid-column: 1 / -1; }
        .edit-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
            gap: 1rem;
        }
        .edit-row:last-child { border-bottom: none; }
        .edit-row .label { color: var(--text-secondary); white-space: nowrap; min-width: 80px; }
        .edit-row .edit-field { flex: 1; text-align: right; }
        .edit-row .edit-input, .edit-row .edit-select { text-align: right; }
        .star-input { display: inline-flex; gap: 0.25rem; cursor: pointer; font-size: 1.5rem; }
        .star-input .star { color: var(--border); transition: color 0.1s; user-select: none; }
        .star-input .star.active { color: var(--accent); }
        .star-input .star:hover { color: var(--accent); }
        .metric-card .edit-input { text-align: center; width: 80px; }
        .menu-wrapper { position: relative; margin-left: auto; }
        .menu-btn {
            background: none;
            border: none;
            color: var(--text);
            font-size: 1.5rem;
            cursor: pointer;
            padding: 0.25rem 0.5rem;
            line-height: 1;
        }
        .menu-btn:hover { color: var(--accent); }
        .menu-dropdown {
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            min-width: max-content;
            display: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 200;
        }
        .menu-dropdown.open { display: block; }
        .menu-item {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--text);
            text-decoration: none;
            white-space: nowrap;
        }
        .menu-item:hover { background: var(--surface-hover); }
        @media (max-width: 600px) {
            .container { padding: 1rem; }
            .chart-wrapper { height: 300px; }
            .metrics-bar { justify-content: center; }
        }
    </style>
</head>)HTML" R"HTML(
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <div class="header-title">
                <h1>%1</h1>
                <div class="subtitle">%2</div>
            </div>
            <button class="edit-btn" id="editBtn" onclick="toggleEditMode()">&#9998; Edit</button>
)HTML" + generateMenuHtml() + R"HTML(
        </div>
    </header>
    <main class="container">
        <div class="metrics-bar">
            <div class="metric-card">
                <div class="value">%3g</div>
                <div class="label">Dose</div>
            </div>
            <div class="metric-card">
                <div class="value">%4</div>
                <div class="label">Yield</div>
            </div>
            <div class="metric-card">
                <div class="value">1:%5</div>
                <div class="label">Ratio</div>
            </div>
            <div class="metric-card">
                <div class="value">%6s</div>
                <div class="label">Time</div>
            </div>
            <div class="metric-card">
                <div class="value rating">%7</div>
                <div class="label">Rating</div>
            </div>
        </div>

        <div class="chart-container">
            <div class="chart-header">
                <div class="chart-title">Extraction Curves</div>
                <div class="chart-toggles">
                    <button class="toggle-btn pressure active" onclick="toggleDataset(0, this)">
                        <span class="dot"></span> Pressure
                    </button>
                    <button class="toggle-btn flow active" onclick="toggleDataset(1, this)">
                        <span class="dot"></span> Flow
                    </button>
                    <button class="toggle-btn weight active" onclick="toggleDataset(2, this)">
                        <span class="dot"></span> Yield
                    </button>
                    <button class="toggle-btn temp active" onclick="toggleDataset(3, this)">
                        <span class="dot"></span> Temp
                    </button>
                    <button class="toggle-btn weightFlow active" onclick="toggleDataset(6, this)">
                        <span class="dot"></span> Weight Flow
                    </button>
                </div>
            </div>
            <div class="chart-wrapper">
                <canvas id="shotChart"></canvas>
            </div>
        </div>

        <div class="info-grid">
            <div class="info-card" style="grid-column:1/-1;">
                <h3>Notes</h3>
                <p class="notes-text">%14</p>
            </div>
            <div class="info-card">
                <h3>Beans (%13)</h3>
                <div class="info-row">
                    <span class="label">Brand</span>
                    <span class="value">%8</span>
                </div>
                <div class="info-row">
                    <span class="label">Type</span>
                    <span class="value">%9</span>
                </div>
                <div class="info-row">
                    <span class="label">Roast Date</span>
                    <span class="value">%10</span>
                </div>
                <div class="info-row">
                    <span class="label">Roast Level</span>
                    <span class="value">%11</span>
                </div>
            </div>
            <div class="info-card">
                <h3>Grinder</h3>
                <div class="info-row">
                    <span class="label">Model</span>
                    <span class="value">%12</span>
                </div>
                <div class="info-row">
                    <span class="label">Setting</span>
                    <span class="value">%13</span>
                </div>
            </div>
        </div>

        <div class="actions-bar" style="margin-top:1.5rem;display:flex;gap:1rem;flex-wrap:wrap;">
            <button onclick="downloadProfile()" style="display:inline-flex;align-items:center;gap:0.5rem;padding:0.75rem 1.25rem;background:var(--surface);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:0.875rem;cursor:pointer;">
                &#128196; Download Profile JSON
            </button>
            <button onclick="var c=document.getElementById('debugLogContainer'); if(c){if(c.style.display==='none'){c.style.display='block';c.scrollIntoView({behavior:'smooth'});}else{c.style.display='none';}}" style="display:inline-flex;align-items:center;gap:0.5rem;padding:0.75rem 1.25rem;background:var(--surface);border:1px solid var(--border);border-radius:8px;color:var(--text);font-size:0.875rem;cursor:pointer;">
                &#128203; View Debug Log
            </button>
        </div>

        <div id="debugLogContainer" style="display:none;margin-top:1rem;">
            <div class="info-card">
                <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:0.75rem;">
                    <h3 style="margin-bottom:0;">Debug Log</h3>
                    <button onclick="copyDebugLog()" style="padding:0.5rem 1rem;background:var(--accent);border:none;border-radius:6px;color:#000;font-weight:500;cursor:pointer;font-size:0.8125rem;">Copy to Clipboard</button>
                </div>
                <pre id="debugLogContent" style="background:var(--bg);padding:1rem;border-radius:8px;overflow-x:auto;font-size:0.75rem;line-height:1.4;white-space:pre-wrap;word-break:break-all;max-height:500px;overflow-y:auto;">%21</pre>
            </div>
        </div>
    </main>

    <div class="edit-bar" id="editBar">
        <button class="save-btn" onclick="saveChanges()">Save</button>
        <button class="cancel-btn" onclick="cancelEdit()">Cancel</button>
    </div>

    <script>
        var shotData = {
            id: %24,
            beanBrand: "%25",
            beanType: "%26",
            roastDate: "%27",
            roastLevel: "%28",
            grinderModel: "%29",
            grinderSetting: "%30",
            espressoNotes: "%31",
            doseWeight: %32,
            finalWeight: %33,
            enjoyment: %34,
            barista: "%35",
            beverageType: "%36",
            drinkTds: %37,
            drinkEy: %38
        };
    </script>
)HTML" R"HTML(

    <script>
        function downloadProfile() {
            window.location.href = window.location.pathname + '/profile.json';
        }
        function showDebugLog() {
            var container = document.getElementById('debugLogContainer');
            if (container) {
                container.style.display = container.style.display === 'none' ? 'block' : 'none';
            } else {
                alert('Debug log container not found');
            }
        }
        function copyDebugLog() {
            var text = document.getElementById('debugLogContent').textContent;
            // Use fallback for non-HTTPS (clipboard API requires secure context)
            var textarea = document.createElement('textarea');
            textarea.value = text;
            textarea.style.position = 'fixed';
            textarea.style.opacity = '0';
            document.body.appendChild(textarea);
            textarea.select();
            try {
                document.execCommand('copy');
            } catch (err) {
                alert('Failed to copy: ' + err);
            }
            document.body.removeChild(textarea);
        }
        var isEditMode = false;
        var originalMetricsHTML = '';
        var originalInfoGridHTML = '';
        var originalActionsDisplay = '';
        var originalDebugDisplay = '';

        function toggleEditMode() {
            if (isEditMode) return;
            isEditMode = true;

            var metricsBar = document.querySelector('.metrics-bar');
            var infoGrid = document.querySelector('.info-grid');
            var actionsBar = document.querySelector('.actions-bar');
            var editBar = document.getElementById('editBar');
            var editBtn = document.getElementById('editBtn');
            var debugContainer = document.getElementById('debugLogContainer');

            originalMetricsHTML = metricsBar.innerHTML;
            originalInfoGridHTML = infoGrid.innerHTML;
            originalActionsDisplay = actionsBar.style.display;

            var stars = shotData.enjoyment > 0 ? Math.round(shotData.enjoyment / 20) : 0;
            var starHtml = '';
            for (var i = 1; i <= 5; i++) {
                starHtml += '<span class="star ' + (i <= stars ? 'active' : '') + '" data-value="' + i + '" onclick="setStarRating(' + i + ')">&#9733;</span>';
            }

            // Build edit form for metrics bar using DOM
            // Note: shotData values are server-escaped and trusted (from our own database)
            var metricsHtml =
                '<div class="metric-card"><input type="number" class="edit-input" id="editDose" step="0.1" value="' + shotData.doseWeight + '" oninput="autoCalcEY()"><div class="label">Dose (g)</div></div>' +
                '<div class="metric-card"><input type="number" class="edit-input" id="editYield" step="0.1" value="' + shotData.finalWeight + '" oninput="autoCalcEY()"><div class="label">Yield (g)</div></div>' +
                '<div class="metric-card"><div class="star-input" id="starRating" data-value="' + stars + '">' + starHtml + '</div><div class="label">Rating</div></div>';
            metricsBar.innerHTML = metricsHtml;

            var roastLevels = ['', 'Light', 'Medium-Light', 'Medium', 'Medium-Dark', 'Dark'];
            var roastOptions = '';
            for (var j = 0; j < roastLevels.length; j++) {
                var rl = roastLevels[j];
                roastOptions += '<option value="' + rl + '"' + (rl === shotData.roastLevel ? ' selected' : '') + '>' + (rl || '\u2014') + '</option>';
            }

            var bevTypes = ['espresso', 'pourover', 'tea', 'other'];
            var bevOptions = '';
            for (var k = 0; k < bevTypes.length; k++) {
                var bt = bevTypes[k];
                bevOptions += '<option value="' + bt + '"' + (bt === shotData.beverageType ? ' selected' : '') + '>' + bt.charAt(0).toUpperCase() + bt.slice(1) + '</option>';
            }

            // Build edit form for info grid
            // All values come from shotData which is server-escaped in the C++ template
            infoGrid.innerHTML =
                '<div class="info-card notes-card-edit"><h3>Notes</h3>' +
                    '<textarea class="edit-textarea" id="editNotes">' + escapeHtml(shotData.espressoNotes) + '</textarea>' +
                '</div>' +
                '<div class="info-card"><h3>Beans</h3>' +
                    '<div class="edit-row"><span class="label">Brand</span><div class="edit-field"><input type="text" class="edit-input" id="editBrand" value="' + escapeAttr(shotData.beanBrand) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">Type</span><div class="edit-field"><input type="text" class="edit-input" id="editType" value="' + escapeAttr(shotData.beanType) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">Roast Date</span><div class="edit-field"><input type="text" class="edit-input" id="editRoastDate" value="' + escapeAttr(shotData.roastDate) + '" placeholder="YYYY-MM-DD"></div></div>' +
                    '<div class="edit-row"><span class="label">Roast Level</span><div class="edit-field"><select class="edit-select" id="editRoastLevel">' + roastOptions + '</select></div></div>' +
                '</div>' +
                '<div class="info-card"><h3>Grinder</h3>' +
                    '<div class="edit-row"><span class="label">Model</span><div class="edit-field"><input type="text" class="edit-input" id="editGrinderModel" value="' + escapeAttr(shotData.grinderModel) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">Setting</span><div class="edit-field"><input type="text" class="edit-input" id="editGrinderSetting" value="' + escapeAttr(shotData.grinderSetting) + '"></div></div>' +
                '</div>' +
                '<div class="info-card"><h3>Additional</h3>' +
                    '<div class="edit-row"><span class="label">Barista</span><div class="edit-field"><input type="text" class="edit-input" id="editBarista" value="' + escapeAttr(shotData.barista) + '"></div></div>' +
                    '<div class="edit-row"><span class="label">Beverage</span><div class="edit-field"><select class="edit-select" id="editBeverageType">' + bevOptions + '</select></div></div>' +
                    '<div class="edit-row"><span class="label">TDS</span><div class="edit-field"><input type="number" class="edit-input" id="editTds" step="0.01" value="' + (shotData.drinkTds || '') + '" oninput="autoCalcEY()"></div></div>' +
                    '<div class="edit-row"><span class="label">EY (%)</span><div class="edit-field"><input type="number" class="edit-input" id="editEy" step="0.1" value="' + (shotData.drinkEy || '') + '" readonly style="opacity:0.7"></div></div>' +
                '</div>';

            actionsBar.style.display = 'none';
            originalDebugDisplay = debugContainer ? debugContainer.style.display : '';
            if (debugContainer) debugContainer.style.display = 'none';
            editBar.classList.add('visible');
            editBtn.style.display = 'none';
            document.querySelector('.container').style.paddingBottom = '5rem';
        }

        function cancelEdit() {
            if (!isEditMode) return;
            isEditMode = false;

            document.querySelector('.metrics-bar').innerHTML = originalMetricsHTML;
            document.querySelector('.info-grid').innerHTML = originalInfoGridHTML;
            document.querySelector('.actions-bar').style.display = originalActionsDisplay;
            document.getElementById('editBar').classList.remove('visible');
            document.getElementById('editBtn').style.display = '';
            document.querySelector('.container').style.paddingBottom = '';
            var debugContainer = document.getElementById('debugLogContainer');
            if (debugContainer) debugContainer.style.display = originalDebugDisplay;
        }

        function setStarRating(value) {
            var stars = document.querySelectorAll('#starRating .star');
            for (var i = 0; i < stars.length; i++) {
                if (i < value) stars[i].classList.add('active');
                else stars[i].classList.remove('active');
            }
            document.getElementById('starRating').dataset.value = value;
        }

        function autoCalcEY() {
            var dose = parseFloat(document.getElementById('editDose').value) || 0;
            var yieldVal = parseFloat(document.getElementById('editYield').value) || 0;
            var tds = parseFloat(document.getElementById('editTds').value) || 0;
            var eyField = document.getElementById('editEy');
            if (dose > 0 && yieldVal > 0 && tds > 0) {
                eyField.value = ((yieldVal * tds) / dose).toFixed(1);
            }
        }

        function saveChanges() {
            var starEl = document.getElementById('starRating');
            var starValue = parseInt(starEl.dataset.value) || 0;

            var data = {
                beanBrand: document.getElementById('editBrand').value,
                beanType: document.getElementById('editType').value,
                roastDate: document.getElementById('editRoastDate').value,
                roastLevel: document.getElementById('editRoastLevel').value,
                grinderModel: document.getElementById('editGrinderModel').value,
                grinderSetting: document.getElementById('editGrinderSetting').value,
                espressoNotes: document.getElementById('editNotes').value,
                doseWeight: parseFloat(document.getElementById('editDose').value) || 0,
                finalWeight: parseFloat(document.getElementById('editYield').value) || 0,
                enjoyment: starValue * 20,
                barista: document.getElementById('editBarista').value,
                beverageType: document.getElementById('editBeverageType').value,
                drinkTds: parseFloat(document.getElementById('editTds').value) || 0,
                drinkEy: parseFloat(document.getElementById('editEy').value) || 0
            };

            var btn = document.querySelector('.save-btn');
            btn.textContent = 'Saving...';
            btn.disabled = true;

            fetch('/api/shot/' + shotData.id + '/metadata', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(data)
            }).then(function(r) { return r.json(); })
            .then(function(result) {
                if (result.success) {
                    window.location.reload();
                } else {
                    alert('Save failed: ' + (result.error || 'Unknown error'));
                    btn.textContent = 'Save';
                    btn.disabled = false;
                }
            }).catch(function(err) {
                alert('Save failed: ' + err);
                btn.textContent = 'Save';
                btn.disabled = false;
            });
        }

        function escapeAttr(s) {
            if (!s) return '';
            return s.replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
        }
        function escapeHtml(s) {
            if (!s) return '';
            return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
        }
    </script>
)HTML" R"HTML(
    <script>
        const pressureData = %15;
        const flowData = %16;
        const weightData = %17;
        const tempData = %18;
        const pressureGoalData = %19;
        const flowGoalData = %20;
        const phaseData = %22;
        const weightFlowRateData = %23;

        // Chart.js plugin: draw vertical phase marker lines and labels
        const phaseMarkerPlugin = {
            id: 'phaseMarkers',
            afterDraw: function(chart) {
                if (!phaseData || phaseData.length === 0) return;
                const ctx = chart.ctx;
                const xScale = chart.scales.x;
                const yScale = chart.scales.y;
                const top = yScale.top;
                const bottom = yScale.bottom;

                ctx.save();
                for (var i = 0; i < phaseData.length; i++) {
                    var marker = phaseData[i];
                    var x = xScale.getPixelForValue(marker.time);
                    if (x < xScale.left || x > xScale.right) continue;

                    // Draw vertical dotted line
                    ctx.beginPath();
                    ctx.setLineDash([3, 3]);
                    ctx.strokeStyle = marker.label === 'End' ? '#FF6B6B' : 'rgba(255,255,255,0.4)';
                    ctx.lineWidth = 1;
                    ctx.moveTo(x, top);
                    ctx.lineTo(x, bottom);
                    ctx.stroke();
                    ctx.setLineDash([]);

                    // Draw label
                    var suffix = '';
                    if (marker.reason === 'weight') suffix = ' [W]';
                    else if (marker.reason === 'pressure') suffix = ' [P]';
                    else if (marker.reason === 'flow') suffix = ' [F]';
                    else if (marker.reason === 'time') suffix = ' [T]';
                    var text = marker.label + suffix;

                    ctx.save();
                    ctx.translate(x + 4, top + 10);
                    ctx.rotate(-Math.PI / 2);
                    ctx.font = (marker.label === 'End' ? 'bold ' : '') + '11px sans-serif';
                    ctx.fillStyle = marker.label === 'End' ? '#FF6B6B' : 'rgba(255,255,255,0.8)';
                    ctx.textAlign = 'right';
                    ctx.fillText(text, 0, 0);
                    ctx.restore();
                }
                ctx.restore();
            }
        };

        // Track mouse position for tooltip
        var mouseX = 0, mouseY = 0;
        document.addEventListener("mousemove", function(e) {
            mouseX = e.pageX;
            mouseY = e.pageY;
        });

        // Find closest data point to a given x value
        function findClosestPoint(data, targetX) {
            if (!data || data.length === 0) return null;
            var closest = data[0];
            var closestDist = Math.abs(data[0].x - targetX);
            for (var i = 1; i < data.length; i++) {
                var dist = Math.abs(data[i].x - targetX);
                if (dist < closestDist) {
                    closestDist = dist;
                    closest = data[i];
                }
            }
            return closest;
        }

        // External tooltip showing all curves
        function externalTooltip(context) {
            var tooltipEl = document.getElementById("chartTooltip");
            if (!tooltipEl) {
                tooltipEl = document.createElement("div");
                tooltipEl.id = "chartTooltip";
                tooltipEl.style.cssText = "position:absolute;background:#161b22;border:1px solid #30363d;border-radius:8px;padding:10px 14px;pointer-events:none;font-size:13px;color:#e6edf3;z-index:100;";
                document.body.appendChild(tooltipEl);
            }

            var tooltip = context.tooltip;
            if (tooltip.opacity === 0) {
                tooltipEl.style.opacity = 0;
                return;
            }

            if (!tooltip.dataPoints || !tooltip.dataPoints.length) {
                tooltipEl.style.opacity = 0;
                return;
            }

            var targetX = tooltip.dataPoints[0].parsed.x;
            var datasets = context.chart.data.datasets;
            var lines = [];)HTML" R"HTML(

            for (var i = 0; i < datasets.length; i++) {
                var ds = datasets[i];
                var meta = context.chart.getDatasetMeta(i);
                if (meta.hidden) continue;

                var pt = findClosestPoint(ds.data, targetX);
                if (!pt || pt.y === null) continue;

                var unit = "";
                if (ds.label.includes("Pressure")) unit = " bar";
                else if (ds.label.includes("Flow")) unit = " ml/s";
                else if (ds.label.includes("Yield")) unit = " g";
                else if (ds.label.includes("Temp")) unit = " °C";

                lines.push('<div style="display:flex;align-items:center;gap:6px;"><span style="display:inline-block;width:12px;height:12px;background:' + ds.borderColor + ';border-radius:2px;"></span>' + ds.label + ': ' + pt.y.toFixed(1) + unit + '</div>');
            }

            tooltipEl.innerHTML = '<div style="font-weight:600;margin-bottom:6px;">' + targetX.toFixed(1) + 's</div>' + lines.join('');
            tooltipEl.style.opacity = 1;
            tooltipEl.style.left = (mouseX + 15) + "px";
            tooltipEl.style.top = (mouseY - 10) + "px";
        }

        const ctx = document.getElementById('shotChart').getContext('2d');
        const chart = new Chart(ctx, {
            type: 'line',
            plugins: [phaseMarkerPlugin],
            data: {
                datasets: [
                    {
                        label: 'Pressure',
                        data: pressureData,
                        borderColor: '#18c37e',
                        backgroundColor: 'rgba(24, 195, 126, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Flow',
                        data: flowData,
                        borderColor: '#4e85f4',
                        backgroundColor: 'rgba(78, 133, 244, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Yield',
                        data: weightData,
                        borderColor: '#a2693d',
                        backgroundColor: 'rgba(162, 105, 61, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y2'
                    },
                    {
                        label: 'Temp',
                        data: tempData,
                        borderColor: '#e73249',
                        backgroundColor: 'rgba(231, 50, 73, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y3'
                    },
                    {
                        label: 'Pressure Goal',
                        data: pressureGoalData,
                        borderColor: '#69fdb3',
                        borderWidth: 1,
                        borderDash: [5, 5],
                        pointRadius: 0,
                        tension: 0.1,
                        yAxisID: 'y',
                        spanGaps: false
                    },
                    {
                        label: 'Flow Goal',
                        data: flowGoalData,
                        borderColor: '#7aaaff',
                        borderWidth: 1,
                        borderDash: [5, 5],
                        pointRadius: 0,
                        tension: 0.1,
                        yAxisID: 'y',
                        spanGaps: false
                    },
                    {
                        label: 'Weight Flow',
                        data: weightFlowRateData,
                        borderColor: '#d4a574',
                        backgroundColor: 'rgba(212, 165, 116, 0.1)',
                        borderWidth: 2,
                        pointRadius: 0,
                        tension: 0.3,
                        yAxisID: 'y'
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: {
                    mode: 'nearest',
                    axis: 'x',
                    intersect: false
                },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        enabled: false,
                        external: externalTooltip
                    }
                },
                scales: {
                    x: {
                        type: 'linear',
                        title: { display: true, text: 'Time (s)', color: '#8b949e' },
                        grid: { color: 'rgba(48, 54, 61, 0.5)' },
                        ticks: { color: '#8b949e' }
                    },
                    y: {
                        type: 'linear',
                        position: 'left',
                        title: { display: true, text: 'Pressure / Flow', color: '#8b949e' },
                        min: 0,
                        max: 12,
                        grid: { color: 'rgba(48, 54, 61, 0.5)' },
                        ticks: { color: '#8b949e' }
                    },)HTML" R"HTML(
                    y2: {
                        type: 'linear',
                        position: 'right',
                        title: { display: true, text: 'Yield (g)', color: '#a2693d' },
                        min: 0,
                        grid: { display: false },
                        ticks: { color: '#a2693d' }
                    },
                    y3: {
                        type: 'linear',
                        position: 'right',
                        title: { display: false },
                        min: 80,
                        max: 100,
                        display: false
                    }
                }
            }
        });

        function toggleDataset(index, btn) {
            const meta = chart.getDatasetMeta(index);
            meta.hidden = !meta.hidden;
            btn.classList.toggle('active');

            // Also toggle goal lines for pressure/flow
            if (index === 0) chart.getDatasetMeta(4).hidden = meta.hidden;
            if (index === 1) chart.getDatasetMeta(5).hidden = meta.hidden;

            chart.update();
        }

        function toggleMenu() {
            var menu = document.getElementById("menuDropdown");
            menu.classList.toggle("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            var btn = e.target.closest(".menu-btn");
            if (!btn && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
        });

        // Power toggle
        var powerState = {awake: false, state: "Unknown"};
        function updatePowerButton() {
            var btn = document.getElementById("powerToggle");
            if (powerState.state === "Unknown" || !powerState.connected) {
                btn.innerHTML = "&#128268; Disconnected";
            } else if (powerState.awake) {
                btn.innerHTML = "&#128164; Put to Sleep";
            } else {
                btn.innerHTML = "&#9889; Wake Up";
            }
        }
        function fetchPowerState() {
            fetch("/api/power/status")
                .then(function(r) { return r.json(); })
                .then(function(data) { powerState = data; updatePowerButton(); })
                .catch(function() {});
        }
        function togglePower() {
            var action = powerState.awake ? "sleep" : "wake";
            fetch("/api/power/" + action)
                .then(function(r) { return r.json(); })
                .then(function() { setTimeout(fetchPowerState, 1000); });
        }
        fetchPowerState();
        var pwrTimer2 = setInterval(fetchPowerState, 5000);
        document.addEventListener('visibilitychange', function() {
            if (document.hidden) { clearInterval(pwrTimer2); }
            else { fetchPowerState(); pwrTimer2 = setInterval(fetchPowerState, 5000); }
        });
    </script>
</body>
</html>
)HTML")
    .arg(tempOverride > 0
         ? shot["profileName"].toString().toHtmlEscaped() + QString(" (%1&deg;C)").arg(tempOverride, 0, 'f', 0)
         : shot["profileName"].toString().toHtmlEscaped())
    .arg(shot["dateTime"].toString())
    .arg(shot["doseWeight"].toDouble(), 0, 'f', 1)
    .arg(yieldDisplay)
    .arg(ratio, 0, 'f', 1)
    .arg(shot["duration"].toDouble(), 0, 'f', 1)
    .arg(stars)
    .arg(shot["beanBrand"].toString().isEmpty() ? "-" : shot["beanBrand"].toString().toHtmlEscaped())
    .arg(shot["beanType"].toString().isEmpty() ? "-" : shot["beanType"].toString().toHtmlEscaped())
    .arg(shot["roastDate"].toString().isEmpty() ? "-" : shot["roastDate"].toString().toHtmlEscaped())
    .arg(shot["roastLevel"].toString().isEmpty() ? "-" : shot["roastLevel"].toString().toHtmlEscaped())
    .arg(shot["grinderModel"].toString().isEmpty() ? "-" : shot["grinderModel"].toString().toHtmlEscaped())
    .arg(shot["grinderSetting"].toString().isEmpty() ? "-" : shot["grinderSetting"].toString().toHtmlEscaped())
    .arg(shot["espressoNotes"].toString().isEmpty() ? "No notes" : shot["espressoNotes"].toString().toHtmlEscaped())
    .arg(pressureData)
    .arg(flowData)
    .arg(weightData)
    .arg(tempData)
    .arg(pressureGoalData)
    .arg(flowGoalData)
    .arg(shot["debugLog"].toString().isEmpty() ? "No debug log available" : shot["debugLog"].toString().toHtmlEscaped())
    .arg(phaseData)
    .arg(weightFlowRateData)
    // shotData JS object fields (%24-%38)
    .arg(shotId)                                                                     // %24 id
    .arg(jsEscape(shot["beanBrand"].toString()))                                     // %25 beanBrand
    .arg(jsEscape(shot["beanType"].toString()))                                      // %26 beanType
    .arg(jsEscape(shot["roastDate"].toString()))                                     // %27 roastDate
    .arg(jsEscape(shot["roastLevel"].toString()))                                    // %28 roastLevel
    .arg(jsEscape(shot["grinderModel"].toString()))                                  // %29 grinderModel
    .arg(jsEscape(shot["grinderSetting"].toString()))                                // %30 grinderSetting
    .arg(jsEscape(shot["espressoNotes"].toString()))                                 // %31 espressoNotes
    .arg(shot["doseWeight"].toDouble(), 0, 'f', 1)                                  // %32 doseWeight
    .arg(shot["finalWeight"].toDouble(), 0, 'f', 1)                                 // %33 finalWeight
    .arg(qRound(shot["enjoyment"].toDouble()))                                       // %34 enjoyment
    .arg(jsEscape(shot["barista"].toString()))                                       // %35 barista
    .arg(jsEscape(shot["beverageType"].toString().isEmpty()
                  ? QStringLiteral("espresso")
                  : shot["beverageType"].toString()))                                // %36 beverageType
    .arg(shot["drinkTds"].toDouble(), 0, 'f', 2)                                    // %37 drinkTds
    .arg(shot["drinkEy"].toDouble(), 0, 'f', 1);                                    // %38 drinkEy
}

QString ShotServer::generateComparisonPage(const QList<qint64>& shotIds) const
{
    // Load all shots
    QList<QVariantMap> shots;
    for (qint64 id : shotIds) {
        QVariantMap shot = m_storage->getShot(id);
        if (!shot.isEmpty()) {
            shots << shot;
        }
    }

    if (shots.size() < 2) {
        return QStringLiteral("<!DOCTYPE html><html><body>Not enough valid shots to compare</body></html>");
    }

    // Colors for each shot (up to 5)
    QStringList shotColors = {"#c9a227", "#e85d75", "#4ecdc4", "#a855f7", "#f97316"};

    auto pointsToJson = [](const QVariantList& points) -> QString {
        QStringList items;
        for (const QVariant& p : points) {
            QVariantMap pt = p.toMap();
            items << QString("{x:%1,y:%2}").arg(pt["x"].toDouble(), 0, 'f', 2).arg(pt["y"].toDouble(), 0, 'f', 2);
        }
        return "[" + items.join(",") + "]";
    };

    // Build datasets for each shot
    QString datasets;
    QString legendItems;
    int shotIndex = 0;

    for (const QVariantMap& shot : std::as_const(shots)) {
        QString color = shotColors[shotIndex % shotColors.size()];
        QString name = shot["profileName"].toString();
        QString date = shot["dateTime"].toString().left(10);
        QString label = QString("%1 (%2)").arg(name, date);

        QString pressureData = pointsToJson(shot["pressure"].toList());
        QString flowData = pointsToJson(shot["flow"].toList());
        QString weightData = pointsToJson(shot["weight"].toList());
        QString tempData = pointsToJson(shot["temperature"].toList());
        QString wfData = pointsToJson(shot["weightFlowRate"].toList());

        // Add datasets for this shot
        datasets += QString(R"HTML(
            { label: "Pressure - %1", data: %2, borderColor: "%3", borderWidth: 2, pointRadius: 0, tension: 0.3, yAxisID: "y", shotIndex: %4, curveType: "pressure" },
            { label: "Flow - %1", data: %5, borderColor: "%3", borderWidth: 2, pointRadius: 0, tension: 0.3, yAxisID: "y", borderDash: [5,3], shotIndex: %4, curveType: "flow" },
            { label: "Yield - %1", data: %6, borderColor: "%3", borderWidth: 2, pointRadius: 0, tension: 0.3, yAxisID: "y2", borderDash: [2,2], shotIndex: %4, curveType: "weight" },
            { label: "Temp - %1", data: %7, borderColor: "%3", borderWidth: 1, pointRadius: 0, tension: 0.3, yAxisID: "y3", borderDash: [8,4], shotIndex: %4, curveType: "temp" },
            { label: "Weight Flow - %1", data: %8, borderColor: "#d4a574", borderWidth: 1.5, pointRadius: 0, tension: 0.3, yAxisID: "y", shotIndex: %4, curveType: "weightFlow" },
        )HTML").arg(label.toHtmlEscaped(), pressureData, color).arg(shotIndex).arg(flowData, weightData, tempData, wfData);

        double ratio = shot["doseWeight"].toDouble() > 0 ?
            shot["finalWeight"].toDouble() / shot["doseWeight"].toDouble() : 0;

        // Build yield text with optional target
        double cmpFinalWeight = shot["finalWeight"].toDouble();
        double cmpYieldOverride = shot["yieldOverride"].toDouble();
        QString cmpYieldText = QString("%1g").arg(cmpFinalWeight, 0, 'f', 1);
        if (cmpYieldOverride > 0 && qAbs(cmpYieldOverride - cmpFinalWeight) > 0.5) {
            cmpYieldText += QString("(%1g)").arg(cmpYieldOverride, 0, 'f', 0);
        }

        // Build profile label with temp: "Profile (Temp°C) (date)"
        double cmpTemp = shot["temperatureOverride"].toDouble();
        QString profileWithTemp = name;
        if (cmpTemp > 0) {
            profileWithTemp += QString(" (%1&deg;C)").arg(cmpTemp, 0, 'f', 0);
        }
        QString legendLabel = QString("%1 (%2)").arg(profileWithTemp, date);

        legendItems += QString(R"HTML(
            <div class="legend-item">
                <span class="legend-color" style="background:%1"></span>
                <div class="legend-info">
                    <div class="legend-name">%2</div>
                    <div class="legend-details">%3 | %4g in | %5 out | 1:%6 | %7s</div>
                </div>
            </div>
        )HTML").arg(color)
               .arg(legendLabel.toHtmlEscaped())
               .arg(date)
               .arg(shot["doseWeight"].toDouble(), 0, 'f', 1)
               .arg(cmpYieldText)
               .arg(ratio, 0, 'f', 1)
               .arg(shot["duration"].toDouble(), 0, 'f', 1);

        shotIndex++;
    }

    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Compare Shots - Decenza DE1</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --pressure: #18c37e;
            --flow: #4e85f4;
            --temp: #e73249;
            --weight: #a2693d;
            --weightFlow: #d4a574;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1400px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.125rem; font-weight: 600; }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1.5rem;
        }
        .chart-container {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1.5rem;
        }
        .chart-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 1rem;
            flex-wrap: wrap;
            gap: 0.75rem;
        }
        .chart-title { font-size: 1rem; font-weight: 600; }
        .curve-toggles {
            display: flex;
            gap: 0.5rem;
            flex-wrap: wrap;
        }
        .toggle-btn {
            padding: 0.5rem 1rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text-secondary);
            font-size: 0.8125rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .toggle-btn:hover { border-color: var(--text-secondary); }
        .toggle-btn.active { background: var(--surface); color: var(--text); border-color: var(--text); }
        .toggle-btn .dot { width: 10px; height: 10px; border-radius: 50%; }
        .toggle-btn.pressure .dot { background: var(--pressure); }
        .toggle-btn.flow .dot { background: var(--flow); }
        .toggle-btn.weight .dot { background: var(--weight); }
        .toggle-btn.temp .dot { background: var(--temp); }
        .toggle-btn.weightFlow .dot { background: var(--weightFlow); }
        .chart-wrapper { position: relative; height: 450px; }
        .legend {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
        }
        .legend-title {
            font-size: 0.875rem;
            font-weight: 600;
            margin-bottom: 0.75rem;
            color: var(--text-secondary);
        }
        .legend-item {
            display: flex;
            align-items: center;
            gap: 0.75rem;
            padding: 0.5rem 0;
            border-bottom: 1px solid var(--border);
        }
        .legend-item:last-child { border-bottom: none; }
        .legend-color {
            width: 16px;
            height: 16px;
            border-radius: 4px;
            flex-shrink: 0;
        }
        .legend-name { font-weight: 500; }
        .legend-details { font-size: 0.75rem; color: var(--text-secondary); }
        .curve-legend {
            display: flex;
            gap: 1.5rem;
            margin-top: 1rem;
            padding-top: 1rem;
            border-top: 1px solid var(--border);
            flex-wrap: wrap;
        }
        .curve-legend-item {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            font-size: 0.75rem;
            color: var(--text-secondary);
        }
        .curve-line {
            width: 24px;
            height: 2px;
        }
        .curve-line.solid { background: var(--text-secondary); }
        .curve-line.dashed { background: repeating-linear-gradient(90deg, var(--text-secondary) 0, var(--text-secondary) 4px, transparent 4px, transparent 7px); }
        .curve-line.dotted { background: repeating-linear-gradient(90deg, var(--text-secondary) 0, var(--text-secondary) 2px, transparent 2px, transparent 5px); }
        .curve-line.longdash { background: repeating-linear-gradient(90deg, var(--text-secondary) 0, var(--text-secondary) 8px, transparent 8px, transparent 12px); }
        .menu-wrapper { position: relative; margin-left: auto; }
        .menu-btn {
            background: none;
            border: none;
            color: var(--text);
            font-size: 1.5rem;
            cursor: pointer;
            padding: 0.25rem 0.5rem;
            line-height: 1;
        }
        .menu-btn:hover { color: var(--accent); }
        .menu-dropdown {
            position: absolute;
            top: 100%;
            right: 0;
            margin-top: 0.5rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            min-width: max-content;
            display: none;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 200;
        }
        .menu-dropdown.open { display: block; }
        .menu-item {
            display: block;
            padding: 0.75rem 1rem;
            color: var(--text);
            text-decoration: none;
            white-space: nowrap;
        }
        .menu-item:hover { background: var(--surface); }
        @media (max-width: 600px) {
            .container { padding: 1rem; }
            .chart-wrapper { height: 350px; }
        }
    </style>
</head>)HTML" R"HTML(
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Compare %1 Shots</h1>
)HTML" + generateMenuHtml() + R"HTML(
        </div>
    </header>
    <main class="container">
        <div class="chart-container">
            <div class="chart-header">
                <div class="chart-title">Extraction Curves</div>
                <div class="curve-toggles">
                    <button class="toggle-btn pressure active" onclick="toggleCurve('pressure', this)">
                        <span class="dot"></span> Pressure
                    </button>
                    <button class="toggle-btn flow active" onclick="toggleCurve('flow', this)">
                        <span class="dot"></span> Flow
                    </button>
                    <button class="toggle-btn weight active" onclick="toggleCurve('weight', this)">
                        <span class="dot"></span> Yield
                    </button>
                    <button class="toggle-btn temp active" onclick="toggleCurve('temp', this)">
                        <span class="dot"></span> Temp
                    </button>
                    <button class="toggle-btn weightFlow active" onclick="toggleCurve('weightFlow', this)">
                        <span class="dot"></span> Weight Flow
                    </button>
                </div>
            </div>
            <div class="chart-wrapper">
                <canvas id="compareChart"></canvas>
            </div>
        </div>
        <div class="legend">
            <div class="legend-title">Shots</div>
            %2
            <div class="curve-legend">
                <div class="curve-legend-item"><span class="curve-line solid"></span> Pressure</div>
                <div class="curve-legend-item"><span class="curve-line dashed"></span> Flow</div>
                <div class="curve-legend-item"><span class="curve-line dotted"></span> Yield</div>
                <div class="curve-legend-item"><span class="curve-line longdash"></span> Temp</div>
                <div class="curve-legend-item"><span class="curve-line solid" style="border-color: var(--weightFlow);"></span> Weight Flow</div>
            </div>
        </div>
    </main>
    <script>
        var visibleCurves = { pressure: true, flow: true, weight: true, temp: true, weightFlow: true };

        // Find closest data point in a dataset to a given x value
        function findClosestPoint(data, targetX) {
            if (!data || data.length === 0) return null;
            var closest = data[0];
            var closestDist = Math.abs(data[0].x - targetX);
            for (var i = 1; i < data.length; i++) {
                var dist = Math.abs(data[i].x - targetX);
                if (dist < closestDist) {
                    closestDist = dist;
                    closest = data[i];
                }
            }
            return closest;
        }

        // Track mouse position for tooltip
        var mouseX = 0, mouseY = 0;
        document.addEventListener("mousemove", function(e) {
            mouseX = e.pageX;
            mouseY = e.pageY;
        });

        // Custom external tooltip
        function externalTooltip(context) {
            var tooltipEl = document.getElementById("chartTooltip");
            if (!tooltipEl) {
                tooltipEl = document.createElement("div");
                tooltipEl.id = "chartTooltip";
                tooltipEl.style.cssText = "position:absolute;background:#161b22;border:1px solid #30363d;border-radius:8px;padding:10px 14px;pointer-events:none;font-size:13px;color:#e6edf3;z-index:100;max-width:400px;";
                document.body.appendChild(tooltipEl);
            }

            var tooltip = context.tooltip;
            if (tooltip.opacity === 0) {
                tooltipEl.style.opacity = 0;
                return;
            }

            // Get x position from the nearest point
            if (!tooltip.dataPoints || !tooltip.dataPoints.length) {
                tooltipEl.style.opacity = 0;
                return;
            }

            var targetX = tooltip.dataPoints[0].parsed.x;
            var datasets = context.chart.data.datasets;

            // Group by shot, collect all curve values at this time
            var shotData = {};
            for (var i = 0; i < datasets.length; i++) {
                var ds = datasets[i];
                var meta = context.chart.getDatasetMeta(i);
                if (meta.hidden || !visibleCurves[ds.curveType]) continue;

                var pt = findClosestPoint(ds.data, targetX);
                if (!pt) continue;

                var key = ds.shotIndex;
                if (!shotData[key]) {
                    shotData[key] = { color: ds.borderColor, label: ds.label.split(" - ")[1] || ds.label, values: {} };
                }
                shotData[key].values[ds.curveType] = pt.y;
            }

            // Build HTML
            var html = "<div style='font-weight:600;margin-bottom:6px;'>" + targetX.toFixed(1) + "s</div>";
            var curveInfo = { pressure: {l:"P", u:"bar"}, flow: {l:"F", u:"ml/s"}, weight: {l:"W", u:"g"}, temp: {l:"T", u:"°C"}, weightFlow: {l:"WF", u:"g/s"} };

            for (var shotIdx in shotData) {
                var shot = shotData[shotIdx];
                var parts = [];
                ["pressure", "flow", "weight", "temp"].forEach(function(ct) {
                    if (shot.values[ct] !== undefined && visibleCurves[ct]) {
                        parts.push("<span style='color:" + shot.color + "'>" + curveInfo[ct].l + ":</span>" + shot.values[ct].toFixed(1) + curveInfo[ct].u);
                    }
                });
                if (parts.length > 0) {
                    html += "<div style='margin-top:4px;'><span style='display:inline-block;width:10px;height:10px;border-radius:2px;background:" + shot.color + ";margin-right:6px;'></span>" + shot.label + "</div>";
                    html += "<div style='color:#8b949e;margin-left:16px;'>" + parts.join(" &nbsp;") + "</div>";
                }
            }

            tooltipEl.innerHTML = html;
            tooltipEl.style.opacity = 1;

            // Position tooltip near mouse cursor (offset to avoid covering cursor)
            tooltipEl.style.left = (mouseX + 15) + "px";
            tooltipEl.style.top = (mouseY - 10) + "px";
        })HTML" R"HTML(

        var ctx = document.getElementById("compareChart").getContext("2d");
        var chart = new Chart(ctx, {
            type: "line",
            data: {
                datasets: [
                    %3
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: { mode: "nearest", axis: "x", intersect: false },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        enabled: false,
                        external: externalTooltip
                    }
                },
                scales: {
                    x: {
                        type: "linear",
                        title: { display: true, text: "Time (s)", color: "#8b949e" },
                        grid: { color: "rgba(48, 54, 61, 0.5)" },
                        ticks: { color: "#8b949e" }
                    },
                    y: {
                        type: "linear",
                        position: "left",
                        title: { display: true, text: "Pressure / Flow", color: "#8b949e" },
                        min: 0, max: 12,
                        grid: { color: "rgba(48, 54, 61, 0.5)" },
                        ticks: { color: "#8b949e" }
                    },
                    y2: {
                        type: "linear",
                        position: "right",
                        title: { display: true, text: "Yield (g)", color: "#a2693d" },
                        min: 0,
                        grid: { display: false },
                        ticks: { color: "#a2693d" }
                    },
                    y3: {
                        type: "linear",
                        position: "right",
                        title: { display: false },
                        min: 80, max: 100,
                        display: false
                    }
                }
            }
        });

        function toggleCurve(curveType, btn) {
            visibleCurves[curveType] = !visibleCurves[curveType];
            btn.classList.toggle("active");

            chart.data.datasets.forEach(function(ds, i) {
                if (ds.curveType === curveType) {
                    chart.getDatasetMeta(i).hidden = !visibleCurves[curveType];
                }
            });
            chart.update();
        }

        function toggleMenu() {
            var menu = document.getElementById("menuDropdown");
            menu.classList.toggle("open");
        }

        document.addEventListener("click", function(e) {
            var menu = document.getElementById("menuDropdown");
            var btn = e.target.closest(".menu-btn");
            if (!btn && menu.classList.contains("open")) {
                menu.classList.remove("open");
            }
        });

        // Power toggle
        var powerState = {awake: false, state: "Unknown"};
        function updatePowerButton() {
            var btn = document.getElementById("powerToggle");
            if (powerState.state === "Unknown" || !powerState.connected) {
                btn.innerHTML = "&#128268; Disconnected";
            } else if (powerState.awake) {
                btn.innerHTML = "&#128164; Put to Sleep";
            } else {
                btn.innerHTML = "&#9889; Wake Up";
            }
        }
        function fetchPowerState() {
            fetch("/api/power/status")
                .then(function(r) { return r.json(); })
                .then(function(data) { powerState = data; updatePowerButton(); })
                .catch(function() {});
        }
        function togglePower() {
            var action = powerState.awake ? "sleep" : "wake";
            fetch("/api/power/" + action)
                .then(function(r) { return r.json(); })
                .then(function() { setTimeout(fetchPowerState, 1000); });
        }
        fetchPowerState();
        var pwrTimer3 = setInterval(fetchPowerState, 5000);
        document.addEventListener('visibilitychange', function() {
            if (document.hidden) { clearInterval(pwrTimer3); }
            else { fetchPowerState(); pwrTimer3 = setInterval(fetchPowerState, 5000); }
        });
    </script>
</body>
</html>
)HTML").arg(shots.size()).arg(legendItems).arg(datasets);
}

QString ShotServer::generateDebugPage() const
{
    return QString(R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Debug &amp; Dev Tools - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            background: var(--bg);
            color: var(--text);
            line-height: 1.5;
        }
        .header {
            background: var(--surface);
            border-bottom: 1px solid var(--border);
            padding: 1rem 1.5rem;
            position: sticky;
            top: 0;
            z-index: 100;
        }
        .header-content {
            max-width: 1400px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            gap: 1rem;
        }
        .back-btn {
            color: var(--text-secondary);
            text-decoration: none;
            font-size: 1.5rem;
        }
        .back-btn:hover { color: var(--accent); }
        h1 { font-size: 1.125rem; font-weight: 600; flex: 1; }
        .status {
            font-size: 0.75rem;
            color: var(--text-secondary);
            display: flex;
            align-items: center;
            gap: 0.5rem;
        }
        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: #18c37e;
            animation: pulse 2s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.5; }
        }
        .controls {
            display: flex;
            gap: 0.5rem;
        }
        .btn {
            padding: 0.5rem 1rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: transparent;
            color: var(--text);
            cursor: pointer;
            font-size: 0.875rem;
        }
        .btn:hover { border-color: var(--accent); color: var(--accent); }
        .btn.active { background: var(--accent); color: var(--bg); border-color: var(--accent); }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            padding: 1rem;
        }
        .log-container {
            background: #000;
            border: 1px solid var(--border);
            border-radius: 8px;
            height: calc(100vh - 120px);
            overflow-y: auto;
            font-family: "Consolas", "Monaco", "Courier New", monospace;
            font-size: 12px;
            padding: 0.5rem;
        }
        .log-line {
            white-space: pre;
            padding: 1px 0;
        }
        .log-line:hover { background: rgba(255,255,255,0.05); }
        .DEBUG { color: #8b949e; }
        .INFO { color: #58a6ff; }
        .WARN { color: #d29922; }
        .ERROR { color: #f85149; }
        .FATAL { color: #ff0000; font-weight: bold; }
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&#8592;</a>
            <h1>Debug &amp; Dev Tools</h1>
            <div class="status">
                <span class="status-dot"></span>
                <span id="lineCount">0 lines</span>
            </div>
            <div class="controls">
                <button class="btn active" id="autoScrollBtn" onclick="toggleAutoScroll()">Auto-scroll</button>
                <button class="btn" onclick="clearLog()">Clear</button>
                <button class="btn" onclick="loadPersistedLog()">Load Saved Log</button>
                <button class="btn" onclick="clearAll()">Clear All</button>
            </div>
        </div>
    </header>
    <main class="container">
        <div style="margin-bottom:1rem;display:flex;gap:0.5rem;flex-wrap:wrap;">
            <a href="/database.db" class="btn" style="text-decoration:none;">&#128190; Download Database</a>
            <a href="/upload" class="btn" style="text-decoration:none;">&#128230; Upload APK</a>
        </div>
        <div class="log-container" id="logContainer"></div>
    </main>
    <script>
        var lastIndex = 0;
        var autoScroll = true;
        var container = document.getElementById("logContainer");
        var lineCountEl = document.getElementById("lineCount");

        function colorize(line) {
            var category = "";
            if (line.includes("] DEBUG ")) category = "DEBUG";
            else if (line.includes("] INFO ")) category = "INFO";
            else if (line.includes("] WARN ")) category = "WARN";
            else if (line.includes("] ERROR ")) category = "ERROR";
            else if (line.includes("] FATAL ")) category = "FATAL";
            return "<div class=\"log-line " + category + "\">" + escapeHtml(line) + "</div>";
        }

        function escapeHtml(text) {
            var div = document.createElement("div");
            div.textContent = text;
            return div.innerHTML;
        }

        function fetchLogs() {
            fetch("/api/debug?after=" + lastIndex)
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (data.lines && data.lines.length > 0) {
                        var html = "";
                        for (var i = 0; i < data.lines.length; i++) {
                            html += colorize(data.lines[i]);
                        }
                        container.insertAdjacentHTML("beforeend", html);
                        if (autoScroll) {
                            container.scrollTop = container.scrollHeight;
                        }
                    }
                    lastIndex = data.lastIndex;
                    lineCountEl.textContent = lastIndex + " lines";
                });
        }

        function toggleAutoScroll() {
            autoScroll = !autoScroll;
            document.getElementById("autoScrollBtn").classList.toggle("active", autoScroll);
            if (autoScroll) {
                container.scrollTop = container.scrollHeight;
            }
        }

        function clearLog() {
            fetch("/api/debug/clear", { method: "POST" })
                .then(function() {
                    container.innerHTML = "";
                    lastIndex = 0;
                });
        }

        function clearAll() {
            if (confirm("Clear both live log and saved log file?")) {
                fetch("/api/debug/clearall", { method: "POST" })
                    .then(function() {
                        container.innerHTML = "";
                        lastIndex = 0;
                    });
            }
        }

        function loadPersistedLog() {
            fetch("/api/debug/file")
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (data.log) {
                        container.innerHTML = "";
                        var lines = data.log.split("\n");
                        var html = "";
                        for (var i = 0; i < lines.length; i++) {
                            if (lines[i]) html += colorize(lines[i]);
                        }
                        container.innerHTML = html;
                        lineCountEl.textContent = lines.length + " lines (from file)";
                        if (autoScroll) {
                            container.scrollTop = container.scrollHeight;
                        }
                    } else {
                        alert("No saved log file found");
                    }
                });
        }

        // Poll every 500ms
        var logTimer = setInterval(fetchLogs, 500);
        document.addEventListener('visibilitychange', function() {
            if (document.hidden) { clearInterval(logTimer); }
            else { fetchLogs(); logTimer = setInterval(fetchLogs, 500); }
        });
        fetchLogs();
    </script>
</body>
</html>
)HTML");
}

