#include "shotserver.h"
#include "../core/settings.h"
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
        {"dyeEyColor", "#a2693d"},
        {"buttonDisabled", "#555555"},
        {"stopMarkerColor", "#FF6B6B"},
        {"frameMarkerColor", "#66ffffff"},
        {"modifiedIndicatorColor", "#FFCC00"},
        {"simulationIndicatorColor", "#E65100"},
        {"warningButtonColor", "#FFA500"},
        {"successButtonColor", "#2E7D32"},
        {"rowAlternateColor", "#1a1a1a"},
        {"rowAlternateLightColor", "#222222"},
        {"sourceBadgeBlueColor", "#4a90d9"},
        {"sourceBadgeGreenColor", "#4ad94a"},
        {"sourceBadgeOrangeColor", "#d9a04a"}
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

    // Colors detected on the current page (set by QML tree walker)
    QJsonArray pageColors;
    for (const QString& colorName : m_settings->currentPageColors()) {
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

    // POST /api/theme/flash - flash a color red/black on device to identify it
    if (path == "/api/theme/flash" && method == "POST") {
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString name = obj["name"].toString();
        if (name.isEmpty()) {
            sendResponse(socket, 400, "text/plain", "Missing name");
            return;
        }
        m_settings->flashThemeColor(name);
        sendResponse(socket, 200, "application/json", "{\"ok\":true}");
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

    // --- Theme Library endpoints (local save/browse/apply) ---

    // POST /api/theme/library/save - save current theme to local library
    if (path == "/api/theme/library/save" && method == "POST") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Widget library not available"})");
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(body).object();
        QString name = obj["name"].toString();
        if (name.isEmpty()) name = m_settings->activeThemeName();
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
