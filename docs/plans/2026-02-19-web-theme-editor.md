# Web Theme Editor Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a web-based theme editor at `/theme` served by ShotServer, with real-time bidirectional sync via SSE.

**Architecture:** Dedicated `/api/theme/*` REST endpoints delegate to existing `Settings` methods. SSE stream (same pattern as layout editor's `m_sseLayoutClients`) pushes `theme-changed` events. Font sizes get new `customFontSizes` persistence in Settings, mirroring how `customThemeColors` works.

**Tech Stack:** Qt/C++ (ShotServer, Settings), QML (Theme.qml), HTML/CSS/JS (web template)

**Design Doc:** `docs/plans/2026-02-19-web-theme-editor-design.md`

---

### Task 1: Add font size persistence to Settings

Add `customFontSizes` QVariantMap property to Settings, mirroring `customThemeColors`.

**Files:**
- Modify: `src/core/settings.h:83-86` (after customThemeColors property)
- Modify: `src/core/settings.h:382-391` (after theme method declarations)
- Modify: `src/core/settings.h:676-679` (signals section)
- Modify: `src/core/settings.cpp:1236` (after customThemeColors implementation)

**Step 1: Add property and method declarations to settings.h**

After line 85 (`Q_PROPERTY(QString activeThemeName ...)`), add:

```cpp
    Q_PROPERTY(QVariantMap customFontSizes READ customFontSizes WRITE setCustomFontSizes NOTIFY customFontSizesChanged)
```

After line 391 (`generatePalette` declaration), add:

```cpp
    // Font size customization
    QVariantMap customFontSizes() const;
    void setCustomFontSizes(const QVariantMap& sizes);
    Q_INVOKABLE void setFontSize(const QString& fontName, int size);
    Q_INVOKABLE int getFontSize(const QString& fontName) const;
    Q_INVOKABLE void resetFontSizesToDefault();
```

After line 678 (`activeThemeNameChanged` signal), add:

```cpp
    void customFontSizesChanged();
```

**Step 2: Implement font size methods in settings.cpp**

After `resetThemeToDefault()` implementation (line 1336), add:

```cpp
// Font size customization
QVariantMap Settings::customFontSizes() const {
    QByteArray data = m_settings.value("theme/customFontSizes").toByteArray();
    if (data.isEmpty()) {
        return QVariantMap();
    }
    QJsonDocument doc = QJsonDocument::fromJson(data);
    return doc.object().toVariantMap();
}

void Settings::setCustomFontSizes(const QVariantMap& sizes) {
    QJsonObject obj = QJsonObject::fromVariantMap(sizes);
    m_settings.setValue("theme/customFontSizes", QJsonDocument(obj).toJson());
    emit customFontSizesChanged();
}

void Settings::setFontSize(const QString& fontName, int size) {
    QVariantMap sizes = customFontSizes();
    sizes[fontName] = size;
    setCustomFontSizes(sizes);
}

int Settings::getFontSize(const QString& fontName) const {
    QVariantMap sizes = customFontSizes();
    return sizes.value(fontName, 0).toInt();
}

void Settings::resetFontSizesToDefault() {
    m_settings.remove("theme/customFontSizes");
    emit customFontSizesChanged();
}
```

**Step 3: Commit**

```bash
git add src/core/settings.h src/core/settings.cpp
git commit -m "feat: add customFontSizes persistence to Settings"
```

---

### Task 2: Bind Theme.qml fonts to Settings

Make font sizes dynamic by reading from `Settings.customFontSizes` with fallback defaults.

**Files:**
- Modify: `qml/Theme.qml:78-86` (font property definitions)

**Step 1: Replace hardcoded font sizes with Settings bindings**

Replace lines 78-86:

```qml
    // Scaled fonts
    readonly property font headingFont: Qt.font({ pixelSize: scaled(32), bold: true })
    readonly property font titleFont: Qt.font({ pixelSize: scaled(24), bold: true })
    readonly property font subtitleFont: Qt.font({ pixelSize: scaled(18), bold: true })
    readonly property font bodyFont: Qt.font({ pixelSize: scaled(18) })
    readonly property font labelFont: Qt.font({ pixelSize: scaled(14) })
    readonly property font captionFont: Qt.font({ pixelSize: scaled(12) })
    readonly property font valueFont: Qt.font({ pixelSize: scaled(48), bold: true })
    readonly property font timerFont: Qt.font({ pixelSize: scaled(72), bold: true })
```

With:

```qml
    // Scaled fonts (sizes customizable via Settings.customFontSizes)
    readonly property font headingFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.headingSize || 32), bold: true })
    readonly property font titleFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.titleSize || 24), bold: true })
    readonly property font subtitleFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.subtitleSize || 18), bold: true })
    readonly property font bodyFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.bodySize || 18) })
    readonly property font labelFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.labelSize || 14) })
    readonly property font captionFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.captionSize || 12) })
    readonly property font valueFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.valueSize || 48), bold: true })
    readonly property font timerFont: Qt.font({ pixelSize: scaled(Settings.customFontSizes.timerSize || 72), bold: true })
```

**Step 2: Commit**

```bash
git add qml/Theme.qml
git commit -m "feat: bind Theme.qml font sizes to Settings for customization"
```

---

### Task 3: Add theme SSE and API routing to ShotServer

Add the SSE infrastructure for theme changes and route new `/api/theme/*` endpoints.

**Files:**
- Modify: `src/network/shotserver.h:85` (add slot)
- Modify: `src/network/shotserver.h:129-130` (add method declarations)
- Modify: `src/network/shotserver.h:153` (add member)
- Modify: `src/network/shotserver.cpp:62-69` (setSettings - add signal connections)
- Modify: `src/network/shotserver.cpp:71-87` (after onLayoutChanged - add onThemeChanged)
- Modify: `src/network/shotserver.cpp:148` (stop - clear theme clients)
- Modify: `src/network/shotserver.cpp:360` (onDisconnected - remove theme clients)
- Modify: `src/network/shotserver.cpp:174` (onReadyRead - skip theme SSE clients)
- Modify: `src/network/shotserver.cpp:879-893` (routing - add theme routes before layout)

**Step 1: Add declarations to shotserver.h**

After line 85 (`void onLayoutChanged();`), add:

```cpp
    void onThemeChanged();
```

After line 130 (`void handleLayoutApi(...);`), add:

```cpp
    // Theme editor web UI
    QString generateThemePage() const;
    void handleThemeApi(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body);
    QJsonObject buildThemeJson() const;
```

After line 153 (`QSet<QTcpSocket*> m_sseLayoutClients;`), add:

```cpp
    QSet<QTcpSocket*> m_sseThemeClients;   // SSE connections for theme change notifications
```

**Step 2: Add SSE infrastructure to shotserver.cpp**

In `setSettings()` (line 62-69), after the existing `connect()` call, add:

```cpp
        connect(m_settings, &Settings::customThemeColorsChanged,
                this, &ShotServer::onThemeChanged);
        connect(m_settings, &Settings::customFontSizesChanged,
                this, &ShotServer::onThemeChanged);
        connect(m_settings, &Settings::activeThemeNameChanged,
                this, &ShotServer::onThemeChanged);
```

After `onLayoutChanged()` (line 87), add:

```cpp
void ShotServer::onThemeChanged()
{
    // Notify all SSE clients that the theme has changed
    QByteArray event = "event: theme-changed\ndata: {}\n\n";
    QList<QTcpSocket*> dead;
    for (QTcpSocket* client : m_sseThemeClients) {
        if (client->state() != QAbstractSocket::ConnectedState) {
            dead.append(client);
            continue;
        }
        client->write(event);
        client->flush();
    }
    for (QTcpSocket* client : dead) {
        m_sseThemeClients.remove(client);
    }
}
```

In `stop()` (near line 148 where `m_sseLayoutClients.clear()` is), add:

```cpp
    m_sseThemeClients.clear();
```

In `onDisconnected()` (near line 360 where `m_sseLayoutClients.remove(socket)` is), add:

```cpp
    m_sseThemeClients.remove(socket);
```

In `onReadyRead()` (near line 174 where `m_sseLayoutClients.contains(socket)` check is), add:

```cpp
    if (m_sseThemeClients.contains(socket)) return;
```

**Step 3: Add routing in handleRequest()**

Before the layout editor block (line 879 `else if (path == "/layout")`), add:

```cpp
    // Theme editor
    else if (path == "/theme") {
        sendHtml(socket, generateThemePage());
    }
    // SSE endpoint for theme change notifications
    else if (path == "/api/theme/subscribe" && method == "GET") {
        QByteArray headers = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "Access-Control-Allow-Origin: *\r\n\r\n";
        socket->write(headers);
        socket->flush();
        m_sseThemeClients.insert(socket);
    }
    // Theme API endpoints
    else if (path == "/api/theme" || path.startsWith("/api/theme/")) {
        qsizetype headerEndPos = request.indexOf("\r\n\r\n");
        QByteArray body = (headerEndPos >= 0) ? request.mid(headerEndPos + 4) : QByteArray();
        handleThemeApi(socket, method, path, body);
    }
```

**Step 4: Commit**

```bash
git add src/network/shotserver.h src/network/shotserver.cpp
git commit -m "feat: add theme SSE infrastructure and API routing to ShotServer"
```

---

### Task 4: Implement theme API handler

Create `shotserver_theme.cpp` with the theme API logic and page generation.

**Files:**
- Create: `src/network/shotserver_theme.cpp`
- Modify: `CMakeLists.txt:200` (add source file after `shotserver_layout.cpp`)

**Step 1: Add to CMakeLists.txt**

After `src/network/shotserver_layout.cpp` (line 200), add:

```
    src/network/shotserver_theme.cpp
```

**Step 2: Create shotserver_theme.cpp with API handler and JSON builder**

Create `src/network/shotserver_theme.cpp`:

```cpp
#include "shotserver.h"
#include "../core/settings.h"
#include "webtemplates/theme_page.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

QJsonObject ShotServer::buildThemeJson() const
{
    QJsonObject result;

    if (!m_settings) {
        return result;
    }

    // Active theme name
    result["activeThemeName"] = m_settings->activeThemeName();

    // All colors
    QJsonObject colors;
    QVariantMap themeColors = m_settings->customThemeColors();

    // Color definitions with defaults (matching Theme.qml)
    static const QMap<QString, QString> colorDefaults = {
        {"backgroundColor", "#1a1a2e"},
        {"surfaceColor", "#303048"},
        {"primaryColor", "#4e85f4"},
        {"secondaryColor", "#c0c5e3"},
        {"textColor", "#ffffff"},
        {"textSecondaryColor", "#a0a8b8"},
        {"accentColor", "#e94560"},
        {"successColor", "#00cc6d"},
        {"warningColor", "#ffaa00"},
        {"highlightColor", "#ffaa00"},
        {"errorColor", "#ff4444"},
        {"borderColor", "#3a3a4e"},
        {"pressureColor", "#18c37e"},
        {"pressureGoalColor", "#69fdb3"},
        {"flowColor", "#4e85f4"},
        {"flowGoalColor", "#7aaaff"},
        {"temperatureColor", "#e73249"},
        {"temperatureGoalColor", "#ffa5a6"},
        {"weightColor", "#a2693d"},
        {"weightFlowColor", "#d4a574"},
        {"dyeDoseColor", "#6F4E37"},
        {"dyeOutputColor", "#9C27B0"},
        {"dyeTdsColor", "#FF9800"},
        {"dyeEyColor", "#a2693d"}
    };

    for (auto it = colorDefaults.constBegin(); it != colorDefaults.constEnd(); ++it) {
        QString val = themeColors.value(it.key()).toString();
        colors[it.key()] = val.isEmpty() ? it.value() : val;
    }
    result["colors"] = colors;

    // Font sizes
    QJsonObject fonts;
    QVariantMap fontSizes = m_settings->customFontSizes();
    static const QMap<QString, int> fontDefaults = {
        {"headingSize", 32},
        {"titleSize", 24},
        {"subtitleSize", 18},
        {"bodySize", 18},
        {"labelSize", 14},
        {"captionSize", 12},
        {"valueSize", 48},
        {"timerSize", 72}
    };

    for (auto it = fontDefaults.constBegin(); it != fontDefaults.constEnd(); ++it) {
        int val = fontSizes.value(it.key()).toInt();
        fonts[it.key()] = val > 0 ? val : it.value();
    }
    result["fonts"] = fonts;

    // Preset themes
    QJsonArray presets;
    QVariantList presetList = m_settings->getPresetThemes();
    for (const QVariant& v : presetList) {
        QVariantMap map = v.toMap();
        QJsonObject preset;
        preset["name"] = map["name"].toString();
        preset["primaryColor"] = map["primaryColor"].toString();
        preset["isBuiltIn"] = map["isBuiltIn"].toBool();
        presets.append(preset);
    }
    result["presets"] = presets;

    return result;
}

void ShotServer::handleThemeApi(QTcpSocket* socket, const QString& method,
                                 const QString& path, const QByteArray& body)
{
    if (!m_settings) {
        sendResponse(socket, 500, "text/plain", "Settings not available");
        return;
    }

    // GET /api/theme - return full theme state
    if (path == "/api/theme" && method == "GET") {
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/color - set a single color
    if (path == "/api/theme/color" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString name = obj["name"].toString();
        QString value = obj["value"].toString();
        if (name.isEmpty() || value.isEmpty()) {
            sendResponse(socket, 400, "text/plain", "Missing name or value");
            return;
        }
        m_settings->setThemeColor(name, value);
        sendResponse(socket, 200, "application/json", "{\"ok\":true}");
        return;
    }

    // POST /api/theme/font - set a single font size
    if (path == "/api/theme/font" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString name = obj["name"].toString();
        int value = obj["value"].toInt();
        if (name.isEmpty() || value <= 0) {
            sendResponse(socket, 400, "text/plain", "Missing name or invalid value");
            return;
        }
        m_settings->setFontSize(name, value);
        sendResponse(socket, 200, "application/json", "{\"ok\":true}");
        return;
    }

    // POST /api/theme/preset - apply a preset theme
    if (path == "/api/theme/preset" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString name = obj["name"].toString();
        if (name.isEmpty()) {
            sendResponse(socket, 400, "text/plain", "Missing name");
            return;
        }
        m_settings->applyPresetTheme(name);
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/palette - generate and apply random palette
    if (path == "/api/theme/palette" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        double hue = obj["hue"].toDouble();
        double saturation = obj["saturation"].toDouble();
        double lightness = obj["lightness"].toDouble();
        QVariantMap palette = m_settings->generatePalette(hue, saturation, lightness);
        m_settings->setCustomThemeColors(palette);
        m_settings->setActiveThemeName("Custom");
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/save - save current theme with name
    if (path == "/api/theme/save" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString name = obj["name"].toString();
        if (name.isEmpty()) {
            sendResponse(socket, 400, "text/plain", "Missing name");
            return;
        }
        m_settings->saveCurrentTheme(name);
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/reset - reset to defaults
    if (path == "/api/theme/reset" && method == "POST") {
        m_settings->resetThemeToDefault();
        m_settings->resetFontSizesToDefault();
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // DELETE /api/theme/preset/{name} - delete a user theme
    if (path.startsWith("/api/theme/preset/") && method == "DELETE") {
        QString name = QUrl::fromPercentEncoding(path.mid(18).toUtf8());
        if (name.isEmpty()) {
            sendResponse(socket, 400, "text/plain", "Missing theme name");
            return;
        }
        m_settings->deleteUserTheme(name);
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    sendResponse(socket, 404, "text/plain", "Not Found");
}

QString ShotServer::generateThemePage() const
{
    return generateThemePageHtml();
}
```

**Step 3: Commit**

```bash
git add src/network/shotserver_theme.cpp CMakeLists.txt
git commit -m "feat: implement theme API handler with all endpoints"
```

---

### Task 5: Create the web template for the theme editor page

Build the HTML/CSS/JS as an inline header file, following the pattern of existing web templates.

**Files:**
- Create: `src/network/webtemplates/theme_page.h`

**Step 1: Create theme_page.h**

Create `src/network/webtemplates/theme_page.h` with the full web UI. This is a large file — the complete HTML/CSS/JS for the theme editor.

The page must:
- Include the standard menu (via `generateMenuHtml()`)
- Use `base_css.h` styles as the foundation
- Have a two-column layout: colors (left), fonts + presets (right)
- Use native `<input type="color">` pickers with hex text inputs
- Use range sliders for font sizes
- Show preset theme buttons with save/delete
- Include a "Random Theme" button
- Connect to SSE at `/api/theme/subscribe` on load
- Debounce color picker changes to ~50ms
- Ignore self-echo SSE events via timestamp tracking

```cpp
#pragma once

#include <QString>
#include "base_css.h"
#include "menu_css.h"
#include "menu_html.h"
#include "menu_js.h"

inline QString generateThemePageHtml()
{
    QString html = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Theme Editor - Decenza</title>
<style>
)HTML";

    html += WEB_CSS_VARIABLES;
    html += WEB_CSS_MENU;

    html += R"HTML(
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
}

.header {
    display: flex;
    align-items: center;
    padding: 16px 20px;
    background: var(--surface);
    border-bottom: 1px solid var(--border);
    position: sticky;
    top: 0;
    z-index: 100;
}
.header h1 {
    font-size: 18px;
    font-weight: 600;
    margin-left: 12px;
    flex: 1;
}
.header .theme-name {
    font-size: 14px;
    color: var(--text-secondary);
    margin-right: 12px;
}

.main {
    display: flex;
    gap: 0;
    max-width: 1200px;
    margin: 0 auto;
    min-height: calc(100vh - 120px);
}

/* Left panel - colors */
.color-panel {
    flex: 1;
    padding: 20px;
    overflow-y: auto;
    border-right: 1px solid var(--border);
    min-width: 300px;
}
.category-title {
    font-size: 13px;
    font-weight: 600;
    color: var(--text-secondary);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-top: 20px;
    margin-bottom: 8px;
}
.category-title:first-child { margin-top: 0; }

.color-row {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 6px 0;
}
.color-swatch {
    width: 32px;
    height: 32px;
    border-radius: 6px;
    border: 2px solid var(--border);
    cursor: pointer;
    position: relative;
    overflow: hidden;
    flex-shrink: 0;
}
.color-swatch input[type="color"] {
    position: absolute;
    top: -4px;
    left: -4px;
    width: 40px;
    height: 40px;
    border: none;
    cursor: pointer;
    opacity: 0;
}
.color-label {
    flex: 1;
    font-size: 14px;
    min-width: 100px;
}
.color-hex {
    font-family: 'SF Mono', 'Fira Code', monospace;
    font-size: 13px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 4px 8px;
    color: var(--text);
    width: 90px;
    text-align: center;
}
.color-hex:focus {
    outline: none;
    border-color: var(--accent);
}

/* Right panel - fonts + presets */
.right-panel {
    flex: 1;
    padding: 20px;
    overflow-y: auto;
    min-width: 300px;
}

.section-title {
    font-size: 15px;
    font-weight: 600;
    margin-bottom: 12px;
    color: var(--text);
}

.font-row {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 8px 0;
}
.font-label {
    width: 80px;
    font-size: 14px;
    color: var(--text-secondary);
}
.font-slider {
    flex: 1;
    -webkit-appearance: none;
    appearance: none;
    height: 4px;
    background: var(--border);
    border-radius: 2px;
    outline: none;
}
.font-slider::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 16px;
    height: 16px;
    border-radius: 50%;
    background: var(--accent);
    cursor: pointer;
}
.font-slider::-moz-range-thumb {
    width: 16px;
    height: 16px;
    border-radius: 50%;
    background: var(--accent);
    cursor: pointer;
    border: none;
}
.font-value {
    width: 50px;
    text-align: right;
    font-size: 13px;
    font-family: monospace;
    color: var(--text-secondary);
}

/* Preset themes */
.presets-section {
    margin-top: 24px;
}
.preset-row {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 12px;
}
.preset-btn {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 8px 14px;
    border-radius: 8px;
    border: 2px solid transparent;
    cursor: pointer;
    font-size: 13px;
    font-weight: 500;
    color: white;
    transition: border-color 0.15s, opacity 0.15s;
}
.preset-btn:hover { opacity: 0.85; }
.preset-btn.active { border-color: white; }
.preset-btn .delete-x {
    margin-left: 4px;
    font-size: 11px;
    opacity: 0.7;
    cursor: pointer;
    padding: 2px 4px;
    border-radius: 50%;
}
.preset-btn .delete-x:hover {
    opacity: 1;
    background: rgba(0,0,0,0.3);
}

/* Action buttons */
.actions {
    display: flex;
    gap: 10px;
    margin-top: 16px;
    flex-wrap: wrap;
}
.btn {
    padding: 10px 18px;
    border-radius: 8px;
    border: 1px solid var(--border);
    background: var(--surface);
    color: var(--text);
    font-size: 14px;
    cursor: pointer;
    transition: background 0.15s;
}
.btn:hover { background: var(--surface-hover); }
.btn-danger {
    border-color: #ff4444;
    color: #ff4444;
}
.btn-danger:hover { background: rgba(255,68,68,0.1); }
.btn-rainbow {
    background: linear-gradient(90deg, #ff6b6b, #ffd93d, #6bcb77, #4d96ff, #9b59b6);
    color: white;
    border: none;
    font-weight: 600;
}
.btn-rainbow:hover { opacity: 0.85; }
.btn-primary {
    background: var(--accent);
    border-color: var(--accent);
    color: white;
}
.btn-primary:hover { opacity: 0.85; }

/* Save dialog */
.save-dialog {
    display: none;
    position: fixed;
    top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(0,0,0,0.6);
    z-index: 200;
    align-items: center;
    justify-content: center;
}
.save-dialog.open { display: flex; }
.save-dialog-inner {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 24px;
    min-width: 300px;
}
.save-dialog-inner h3 { margin-bottom: 12px; }
.save-dialog-inner input {
    width: 100%;
    padding: 10px;
    border: 1px solid var(--border);
    border-radius: 6px;
    background: var(--bg);
    color: var(--text);
    font-size: 14px;
    margin-bottom: 12px;
}
.save-dialog-inner input:focus { outline: none; border-color: var(--accent); }
.save-dialog-btns { display: flex; gap: 8px; justify-content: flex-end; }

/* Footer */
.footer {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 16px 20px;
    border-top: 1px solid var(--border);
    background: var(--surface);
}

/* Responsive */
@media (max-width: 700px) {
    .main { flex-direction: column; }
    .color-panel { border-right: none; border-bottom: 1px solid var(--border); }
}
</style>
</head>
<body>

<div class="header">
)HTML";

    html += generateMenuHtml();

    html += R"HTML(
    <h1>Theme Editor</h1>
    <span class="theme-name" id="themeName">Default</span>
</div>

<div class="main">
    <!-- Left: Colors -->
    <div class="color-panel" id="colorPanel"></div>

    <!-- Right: Fonts + Presets -->
    <div class="right-panel">
        <div class="section-title">Font Sizes</div>
        <div id="fontPanel"></div>

        <div class="presets-section">
            <div class="section-title">Presets</div>
            <div class="preset-row" id="presetRow"></div>

            <div class="actions">
                <button class="btn btn-rainbow" onclick="randomTheme()">Random Theme</button>
                <button class="btn btn-primary" onclick="openSaveDialog()">Save Theme</button>
            </div>
        </div>
    </div>
</div>

<div class="footer">
    <button class="btn btn-danger" onclick="resetTheme()">Reset to Default</button>
</div>

<!-- Save Theme Dialog -->
<div class="save-dialog" id="saveDialog">
    <div class="save-dialog-inner">
        <h3>Save Theme</h3>
        <input type="text" id="saveNameInput" placeholder="Theme name..." onkeydown="if(event.key==='Enter') saveTheme()">
        <div class="save-dialog-btns">
            <button class="btn" onclick="closeSaveDialog()">Cancel</button>
            <button class="btn btn-primary" onclick="saveTheme()">Save</button>
        </div>
    </div>
</div>

<script>
)HTML";

    html += WEB_JS_MENU;

    html += R"HTML(
// Color definitions (matching Theme.qml categories)
const COLOR_DEFS = [
    { category: "Core UI", colors: [
        { name: "backgroundColor", display: "Background" },
        { name: "surfaceColor", display: "Surface" },
        { name: "primaryColor", display: "Primary" },
        { name: "secondaryColor", display: "Secondary" },
        { name: "textColor", display: "Text" },
        { name: "textSecondaryColor", display: "Text Secondary" },
        { name: "accentColor", display: "Accent" },
        { name: "borderColor", display: "Border" }
    ]},
    { category: "Status", colors: [
        { name: "successColor", display: "Success" },
        { name: "warningColor", display: "Warning" },
        { name: "highlightColor", display: "Highlight" },
        { name: "errorColor", display: "Error" }
    ]},
    { category: "Chart", colors: [
        { name: "pressureColor", display: "Pressure" },
        { name: "pressureGoalColor", display: "Pressure Goal" },
        { name: "flowColor", display: "Flow" },
        { name: "flowGoalColor", display: "Flow Goal" },
        { name: "temperatureColor", display: "Temperature" },
        { name: "temperatureGoalColor", display: "Temp Goal" },
        { name: "weightColor", display: "Weight" },
        { name: "weightFlowColor", display: "Weight Flow" }
    ]},
    { category: "DYE Metadata", colors: [
        { name: "dyeDoseColor", display: "Dose" },
        { name: "dyeOutputColor", display: "Output" },
        { name: "dyeTdsColor", display: "TDS" },
        { name: "dyeEyColor", display: "EY" }
    ]}
];

const FONT_DEFS = [
    { name: "headingSize", display: "Heading", min: 16, max: 64 },
    { name: "titleSize", display: "Title", min: 12, max: 48 },
    { name: "subtitleSize", display: "Subtitle", min: 10, max: 36 },
    { name: "bodySize", display: "Body", min: 10, max: 36 },
    { name: "labelSize", display: "Label", min: 8, max: 28 },
    { name: "captionSize", display: "Caption", min: 8, max: 24 },
    { name: "valueSize", display: "Value", min: 24, max: 96 },
    { name: "timerSize", display: "Timer", min: 36, max: 120 }
];

let currentTheme = null;
let lastChangeTime = 0;  // To ignore self-echo SSE events
let debounceTimers = {};

// ── Rendering ──

function renderColors(colors) {
    const panel = document.getElementById('colorPanel');
    panel.innerHTML = '';
    for (const cat of COLOR_DEFS) {
        const title = document.createElement('div');
        title.className = 'category-title';
        title.textContent = cat.category;
        panel.appendChild(title);

        for (const c of cat.colors) {
            const val = colors[c.name] || '#000000';
            const row = document.createElement('div');
            row.className = 'color-row';
            row.innerHTML = `
                <div class="color-swatch" style="background:${val}">
                    <input type="color" value="${val}" data-name="${c.name}"
                           oninput="onColorInput(this)" onchange="onColorChange(this)">
                </div>
                <span class="color-label">${c.display}</span>
                <input type="text" class="color-hex" value="${val}" data-name="${c.name}"
                       oninput="onHexInput(this)">
            `;
            panel.appendChild(row);
        }
    }
}

function renderFonts(fonts) {
    const panel = document.getElementById('fontPanel');
    panel.innerHTML = '';
    for (const f of FONT_DEFS) {
        const val = fonts[f.name] || 16;
        const row = document.createElement('div');
        row.className = 'font-row';
        row.innerHTML = `
            <span class="font-label">${f.display}</span>
            <input type="range" class="font-slider" min="${f.min}" max="${f.max}" value="${val}"
                   data-name="${f.name}" oninput="onFontSlider(this)">
            <span class="font-value" id="fv_${f.name}">${val}px</span>
        `;
        panel.appendChild(row);
    }
}

function renderPresets(presets, activeName) {
    const row = document.getElementById('presetRow');
    row.innerHTML = '';
    document.getElementById('themeName').textContent = activeName;

    for (const p of presets) {
        const btn = document.createElement('button');
        btn.className = 'preset-btn' + (p.name === activeName ? ' active' : '');
        btn.style.background = p.primaryColor || '#4e85f4';
        btn.innerHTML = p.name;
        if (!p.isBuiltIn) {
            btn.innerHTML += `<span class="delete-x" onclick="event.stopPropagation(); deletePreset('${p.name.replace(/'/g, "\\'")}')">x</span>`;
        }
        btn.onclick = () => applyPreset(p.name);
        row.appendChild(btn);
    }
}

function renderAll(data) {
    currentTheme = data;
    renderColors(data.colors);
    renderFonts(data.fonts);
    renderPresets(data.presets, data.activeThemeName);
}

// ── API calls ──

async function fetchTheme() {
    const res = await fetch('/api/theme');
    const data = await res.json();
    renderAll(data);
}

function postJson(url, body) {
    lastChangeTime = Date.now();
    return fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
    });
}

function onColorInput(el) {
    // Live preview: update swatch and hex immediately
    const name = el.dataset.name;
    const val = el.value;
    el.parentElement.style.background = val;
    const hexInput = el.closest('.color-row').querySelector('.color-hex');
    if (hexInput) hexInput.value = val;

    // Debounced POST
    clearTimeout(debounceTimers[name]);
    debounceTimers[name] = setTimeout(() => {
        postJson('/api/theme/color', { name, value: val });
    }, 50);
}

function onColorChange(el) {
    // Final value on picker close
    const name = el.dataset.name;
    clearTimeout(debounceTimers[name]);
    postJson('/api/theme/color', { name, value: el.value });
}

function onHexInput(el) {
    const hex = el.value.trim();
    if (/^#[0-9a-fA-F]{6}$/.test(hex)) {
        const name = el.dataset.name;
        const swatch = el.closest('.color-row').querySelector('.color-swatch');
        swatch.style.background = hex;
        swatch.querySelector('input[type="color"]').value = hex;
        clearTimeout(debounceTimers[name]);
        debounceTimers[name] = setTimeout(() => {
            postJson('/api/theme/color', { name, value: hex });
        }, 50);
    }
}

function onFontSlider(el) {
    const name = el.dataset.name;
    const val = parseInt(el.value);
    document.getElementById('fv_' + name).textContent = val + 'px';

    clearTimeout(debounceTimers['font_' + name]);
    debounceTimers['font_' + name] = setTimeout(() => {
        postJson('/api/theme/font', { name, value: val });
    }, 100);
}

async function applyPreset(name) {
    const res = await postJson('/api/theme/preset', { name });
    const data = await res.json();
    renderAll(data);
}

async function randomTheme() {
    const hue = Math.random() * 360;
    const sat = 65 + Math.random() * 20;
    const light = 50 + Math.random() * 10;
    const res = await postJson('/api/theme/palette', { hue, saturation: sat, lightness: light });
    const data = await res.json();
    renderAll(data);
}

async function resetTheme() {
    if (!confirm('Reset theme to defaults?')) return;
    const res = await postJson('/api/theme/reset', {});
    const data = await res.json();
    renderAll(data);
}

async function deletePreset(name) {
    if (!confirm('Delete theme "' + name + '"?')) return;
    lastChangeTime = Date.now();
    const res = await fetch('/api/theme/preset/' + encodeURIComponent(name), { method: 'DELETE' });
    const data = await res.json();
    renderAll(data);
}

function openSaveDialog() {
    document.getElementById('saveDialog').classList.add('open');
    const input = document.getElementById('saveNameInput');
    input.value = '';
    input.focus();
}

function closeSaveDialog() {
    document.getElementById('saveDialog').classList.remove('open');
}

async function saveTheme() {
    const name = document.getElementById('saveNameInput').value.trim();
    if (!name) return;
    closeSaveDialog();
    const res = await postJson('/api/theme/save', { name });
    const data = await res.json();
    renderAll(data);
}

// ── SSE for real-time sync ──

function connectSSE() {
    const evtSource = new EventSource('/api/theme/subscribe');

    evtSource.addEventListener('theme-changed', () => {
        // Ignore if this was our own change (within 200ms)
        if (Date.now() - lastChangeTime < 200) return;
        fetchTheme();
    });

    evtSource.onerror = () => {
        evtSource.close();
        setTimeout(connectSSE, 3000);
    };
}

// ── Init ──
fetchTheme();
connectSSE();
</script>
</body>
</html>)HTML";

    return html;
}
```

**Step 2: Commit**

```bash
git add src/network/webtemplates/theme_page.h
git commit -m "feat: add web theme editor HTML/CSS/JS template"
```

---

### Task 6: Add Theme Editor to navigation menu

Add a link in the burger menu so users can discover the theme editor.

**Files:**
- Modify: `src/network/webtemplates/menu_html.h:32` (after Layout Editor link)

**Step 1: Add menu item**

After line 32 (`<a href="/layout" class="menu-item">&#9998; Layout Editor</a>`), add:

```html
                        <a href="/theme" class="menu-item">&#127912; Theme Editor</a>
```

Note: `&#127912;` is the artist palette emoji.

**Step 2: Commit**

```bash
git add src/network/webtemplates/menu_html.h
git commit -m "feat: add Theme Editor link to web navigation menu"
```

---

### Task 7: Final integration verification

Verify everything compiles and works together.

**Step 1: Verify all files are consistent**

Check that:
- `shotserver_theme.cpp` includes the correct headers
- `buildThemeJson()` color defaults match `Theme.qml` lines 49-76
- `handleThemeApi()` path strings match what the JS sends
- The `generateThemePage()` function name matches what's called in `shotserver.cpp` routing
- `resetFontSizesToDefault()` is called alongside `resetThemeToDefault()` in the reset endpoint

**Step 2: Build (user builds in Qt Creator)**

Let the user build in Qt Creator (per CLAUDE.md: don't build automatically).

**Step 3: Test manually**

1. Run the app, enable Remote Access in Settings
2. Open `http://<device-ip>:8888/theme` in a browser
3. Verify color pickers show current theme colors
4. Change a color → app should update in real-time
5. Change a color on the app → web should update via SSE
6. Test font sliders → app fonts should resize
7. Test presets → both sides should sync
8. Test Random Theme → both sides should sync
9. Test Save/Delete theme
10. Test Reset to Default

**Step 4: Final commit with all files if any fixes were needed**

```bash
git add -A
git commit -m "feat: web-based theme editor with real-time bidirectional sync"
```
