#include "shotserver.h"
#include "../core/settings.h"
#include "../core/settings_theme.h"
#include "../core/widgetlibrary.h"
#include "webtemplates/theme_page.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

QJsonObject ShotServer::buildThemeJson() const
{
    QJsonObject result;

    if (!m_settings) {
        return result;
    }

    // Active theme name
    result["activeThemeName"] = m_settings->theme()->activeThemeName();

    // Screen effect (structured: active + per-effect params)
    result["screenEffect"] = m_settings->theme()->screenEffectJson();

    // Theme mode
    result["themeMode"] = m_settings->theme()->themeMode();
    result["darkThemeName"] = m_settings->theme()->darkThemeName();
    result["lightThemeName"] = m_settings->theme()->lightThemeName();
    result["editingPalette"] = m_settings->theme()->editingPalette();

    // Active colors (resolved for current mode)
    QJsonObject colors = QJsonObject::fromVariantMap(m_settings->theme()->customThemeColors());
    result["colors"] = colors;

    // Editing palette colors (for the color grid)
    QJsonObject editingColors = QJsonObject::fromVariantMap(m_settings->theme()->editingPaletteColors());
    result["editingColors"] = editingColors;

    // Both palettes for reference
    QJsonObject colorsDark = QJsonObject::fromVariantMap(SettingsTheme::darkDefaults());
    QJsonObject colorsLight = QJsonObject::fromVariantMap(SettingsTheme::lightDefaults());
    result["colorsDark"] = colorsDark;
    result["colorsLight"] = colorsLight;

    // Font sizes
    QJsonObject fonts;
    QVariantMap fontSizes = m_settings->theme()->customFontSizes();
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
    QVariantList presetList = m_settings->theme()->getPresetThemes();
    for (const QVariant& v : presetList) {
        QVariantMap map = v.toMap();
        QJsonObject preset;
        preset["name"] = map["name"].toString();
        preset["primaryColor"] = map["primaryColor"].toString();
        preset["isBuiltIn"] = map["isBuiltIn"].toBool();
        presets.append(preset);
    }
    result["presets"] = presets;

    // Colors detected on the current page (set by QML tree walker)
    QJsonArray pageColors;
    for (const QString& colorName : m_settings->theme()->currentPageColors()) {
        pageColors.append(colorName);
    }
    result["pageColors"] = pageColors;

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

    // GET /api/theme/shader - get active shader
    if (path == "/api/theme/shader" && method == "GET") {
        QJsonObject resp;
        resp["shader"] = m_settings->theme()->activeShader();
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/shader - set active shader (empty string = none)
    if (path == "/api/theme/shader" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString shader = obj["shader"].toString();
        m_settings->theme()->setActiveShader(shader);
        QJsonObject resp;
        resp["ok"] = true;
        resp["shader"] = shader;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // GET /api/theme/shader/params - get all shader parameters
    if (path == "/api/theme/shader/params" && method == "GET") {
        QJsonObject resp = QJsonObject::fromVariantMap(m_settings->theme()->shaderParams());
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/shader/params - set one or more shader parameters
    if (path == "/api/theme/shader/params" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            m_settings->theme()->setShaderParam(it.key(), it.value().toDouble());
        }
        QJsonObject resp;
        resp["ok"] = true;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/flash - flash a color red/black on device to identify it
    if (path == "/api/theme/flash" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString name = obj["name"].toString();
        if (name.isEmpty()) {
            sendResponse(socket, 400, "text/plain", "Missing name");
            return;
        }
        m_settings->theme()->flashThemeColor(name);
        sendResponse(socket, 200, "application/json", "{\"ok\":true}");
        return;
    }

    // POST /api/theme/mode - set theme mode (dark/light/system)
    if (path == "/api/theme/mode" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString mode = obj["mode"].toString();
        if (mode != "dark" && mode != "light" && mode != "system") {
            sendResponse(socket, 400, "text/plain", "Invalid mode (dark/light/system)");
            return;
        }
        m_settings->theme()->setThemeMode(mode);
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/editing-palette - switch which palette the editor targets
    if (path == "/api/theme/editing-palette" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString palette = obj["palette"].toString();
        m_settings->theme()->setEditingPalette(palette);
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/color - set a single color (on editing palette)
    if (path == "/api/theme/color" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString name = obj["name"].toString();
        QString value = obj["value"].toString();
        if (name.isEmpty() || value.isEmpty()) {
            sendResponse(socket, 400, "text/plain", "Missing name or value");
            return;
        }
        // Optional palette param to target a specific palette
        if (obj.contains("palette")) {
            m_settings->theme()->setEditingPalette(obj["palette"].toString());
        }
        m_settings->theme()->setEditingPaletteColor(name, value);
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
        m_settings->theme()->setFontSize(name, value);
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
        m_settings->theme()->applyPresetTheme(name);
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/palette - generate and apply random palette to editing palette
    if (path == "/api/theme/palette" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        double hue = obj["hue"].toDouble();
        double saturation = obj["saturation"].toDouble();
        double lightness = obj["lightness"].toDouble();
        QVariantMap palette = m_settings->theme()->generatePalette(hue, saturation, lightness);
        // Write each color to the editing palette (not the active palette)
        for (auto it = palette.constBegin(); it != palette.constEnd(); ++it) {
            m_settings->theme()->setEditingPaletteColor(it.key(), it.value().toString());
        }
        m_settings->theme()->setActiveThemeName("Custom");
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
        m_settings->theme()->saveCurrentTheme(name);
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/reset - reset to defaults
    if (path == "/api/theme/reset" && method == "POST") {
        m_settings->theme()->resetThemeToDefault();
        m_settings->theme()->resetFontSizesToDefault();
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
        m_settings->theme()->deleteUserTheme(name);
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // --- Theme Library endpoints (local save/browse/apply) ---

    // POST /api/theme/library/save - save current theme to local library
    if (path == "/api/theme/library/save" && method == "POST") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Widget library not available"})");
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString name = obj["name"].toString();
        if (name.isEmpty()) name = m_settings->theme()->activeThemeName();
        QString entryId = m_widgetLibrary->addCurrentTheme(name);
        if (entryId.isEmpty()) {
            sendJson(socket, R"({"error":"Failed to save theme"})");
            return;
        }
        QJsonObject resp;
        resp["success"] = true;
        resp["entryId"] = entryId;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // GET /api/theme/library/list - list local theme entries
    if (path == "/api/theme/library/list" && method == "GET") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Widget library not available"})");
            return;
        }
        QVariantList themes = m_widgetLibrary->entriesByType("theme");
        QJsonArray arr;
        for (const QVariant& v : themes) {
            QVariantMap entry = v.toMap();
            QJsonObject obj;
            QString id = entry["id"].toString();
            obj["id"] = id;
            obj["type"] = entry["type"].toString();
            obj["createdAt"] = entry["createdAt"].toString();
            // Extract theme name from data.theme.name
            QVariantMap data = entry["data"].toMap();
            QVariantMap themeData = data["theme"].toMap();
            obj["name"] = themeData["name"].toString();
            // Tags (stored as QVariantList in index)
            QVariantList tagList = entry["tags"].toList();
            QJsonArray tagsArr;
            for (const QVariant& tag : tagList)
                tagsArr.append(tag.toString());
            obj["tags"] = tagsArr;
            obj["hasThumbnail"] = m_widgetLibrary->hasThumbnail(id);
            arr.append(obj);
        }
        QJsonObject resp;
        resp["success"] = true;
        resp["entries"] = arr;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/library/apply - apply a theme from local library
    if (path == "/api/theme/library/apply" && method == "POST") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Widget library not available"})");
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString entryId = obj["entryId"].toString();
        if (entryId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing entryId"})");
            return;
        }
        bool ok = m_widgetLibrary->applyThemeEntry(entryId);
        if (!ok) {
            sendJson(socket, R"({"error":"Failed to apply theme"})");
            return;
        }
        // Return updated theme state so the editor can refresh
        QJsonDocument doc(buildThemeJson());
        sendJson(socket, doc.toJson(QJsonDocument::Compact));
        return;
    }

    // POST /api/theme/library/rename - rename a theme entry
    if (path == "/api/theme/library/rename" && method == "POST") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Widget library not available"})");
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString entryId = obj["entryId"].toString();
        QString newName = obj["name"].toString();
        if (entryId.isEmpty() || newName.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing entryId or name"})");
            return;
        }
        bool ok = m_widgetLibrary->updateThemeName(entryId, newName);
        QJsonObject resp;
        resp["success"] = ok;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // DELETE /api/theme/library/{id} - remove a theme from local library
    if (path.startsWith("/api/theme/library/") && method == "DELETE") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Widget library not available"})");
            return;
        }
        QString entryId = path.mid(19); // after "/api/theme/library/"
        if (entryId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing entry ID"})");
            return;
        }
        bool ok = m_widgetLibrary->removeEntry(entryId);
        QJsonObject resp;
        resp["success"] = ok;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // GET /api/theme/library/{id}/thumbnail - serve theme thumbnail
    if (path.startsWith("/api/theme/library/") && path.endsWith("/thumbnail") && method == "GET") {
        if (!m_widgetLibrary) {
            sendResponse(socket, 404, "text/plain", "Not available");
            return;
        }
        // Extract ID: "/api/theme/library/{id}/thumbnail"
        QString sub = path.mid(19); // after "/api/theme/library/"
        QString entryId = sub.left(sub.length() - 10); // remove "/thumbnail"
        if (m_widgetLibrary->hasThumbnail(entryId)) {
            sendFile(socket, m_widgetLibrary->thumbnailPath(entryId), "image/png");
        } else {
            sendResponse(socket, 404, "text/plain", "No thumbnail");
        }
        return;
    }

    // GET /api/theme/library/{id}/data - get full theme entry data
    if (path.startsWith("/api/theme/library/") && path.endsWith("/data") && method == "GET") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Widget library not available"})");
            return;
        }
        QString sub = path.mid(19); // after "/api/theme/library/"
        QString entryId = sub.left(sub.length() - 5); // remove "/data"
        QVariantMap data = m_widgetLibrary->getEntryData(entryId);
        if (data.isEmpty()) {
            sendResponse(socket, 404, "application/json", R"({"error":"Entry not found"})");
            return;
        }
        QJsonObject resp = QJsonObject::fromVariantMap(data);
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    sendResponse(socket, 404, "text/plain", "Not Found");
}

QString ShotServer::generateThemePage() const
{
    QString deviceId = m_settings ? m_settings->deviceId() : QString();
    return generateThemePageHtml(deviceId);
}
