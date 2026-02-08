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
#include "../core/widgetlibrary.h"
#include "librarysharing.h"
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

void ShotServer::handleLayoutApi(QTcpSocket* socket, const QString& method, const QString& path, const QByteArray& body)
{
    if (!m_settings) {
        sendResponse(socket, 500, "application/json", R"({"error":"Settings not available"})");
        return;
    }

    // GET /api/layout — return current layout configuration
    if (method == "GET" && (path == "/api/layout" || path == "/api/layout/")) {
        QString json = m_settings->layoutConfiguration();
        sendJson(socket, json.toUtf8());
        return;
    }

    // GET /api/layout/item?id=X — return item properties
    if (method == "GET" && path.startsWith("/api/layout/item")) {
        QString itemId;
        int qIdx = path.indexOf("?id=");
        if (qIdx >= 0) {
            itemId = QUrl::fromPercentEncoding(path.mid(qIdx + 4).toUtf8());
        }
        if (itemId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing id parameter"})");
            return;
        }
        QVariantMap props = m_settings->getItemProperties(itemId);
        sendJson(socket, QJsonDocument(QJsonObject::fromVariantMap(props)).toJson(QJsonDocument::Compact));
        return;
    }

    // GET /api/library/entries — list all local library entries
    if (method == "GET" && path == "/api/library/entries") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QVariantList entries = m_widgetLibrary->entries();
        QJsonArray arr;
        for (const QVariant& v : entries)
            arr.append(QJsonObject::fromVariantMap(v.toMap()));
        sendJson(socket, QJsonDocument(arr).toJson(QJsonDocument::Compact));
        return;
    }

    // GET /api/library/thumbnail?id=X — serve thumbnail PNG
    if (method == "GET" && path.startsWith("/api/library/thumbnail")) {
        if (!m_widgetLibrary) {
            sendResponse(socket, 404, "text/plain", "Not found");
            return;
        }
        int qIdx = path.indexOf("?id=");
        QString entryId = (qIdx >= 0) ? QUrl::fromPercentEncoding(path.mid(qIdx + 4).toUtf8()) : QString();
        if (!entryId.isEmpty() && m_widgetLibrary->hasThumbnail(entryId)) {
            sendFile(socket, m_widgetLibrary->thumbnailPath(entryId), "image/png");
        } else {
            sendResponse(socket, 404, "text/plain", "No thumbnail");
        }
        return;
    }

    // GET /api/library/entry?id=X — get full entry data
    if (method == "GET" && path.startsWith("/api/library/entry")) {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        int qIdx = path.indexOf("?id=");
        QString entryId = (qIdx >= 0) ? QUrl::fromPercentEncoding(path.mid(qIdx + 4).toUtf8()) : QString();
        if (entryId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing id parameter"})");
            return;
        }
        QVariantMap data = m_widgetLibrary->getEntryData(entryId);
        if (data.isEmpty()) {
            sendResponse(socket, 404, "application/json", R"({"error":"Entry not found"})");
            return;
        }
        sendJson(socket, QJsonDocument(QJsonObject::fromVariantMap(data)).toJson(QJsonDocument::Compact));
        return;
    }

    // GET /api/community/browse — browse community entries (async)
    if (method == "GET" && path.startsWith("/api/community/browse")) {
        if (!m_librarySharing) {
            sendJson(socket, R"({"error":"Community sharing not available"})");
            return;
        }
        QUrl url("http://localhost" + path);
        QUrlQuery query(url);
        QString type = query.queryItemValue("type");
        QString search = query.queryItemValue("search");
        QString sort = query.queryItemValue("sort", QUrl::FullyDecoded);
        int page = query.queryItemValue("page").toInt();
        if (page < 1) page = 1;
        if (sort.isEmpty()) sort = "newest";

        m_pendingLibrarySocket = socket;

        QMetaObject::Connection* browseConn = new QMetaObject::Connection();
        QMetaObject::Connection* errorConn = new QMetaObject::Connection();

        *browseConn = connect(m_librarySharing, &LibrarySharing::communityEntriesChanged, this, [=]() {
            if (!m_pendingLibrarySocket) return;
            QJsonObject resp;
            resp["success"] = true;
            QVariantList entries = m_librarySharing->communityEntries();
            QJsonArray arr;
            for (const QVariant& v : entries)
                arr.append(QJsonObject::fromVariantMap(v.toMap()));
            resp["entries"] = arr;
            resp["total"] = m_librarySharing->totalCommunityResults();
            sendJson(m_pendingLibrarySocket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
            m_pendingLibrarySocket = nullptr;
            disconnect(*browseConn);
            disconnect(*errorConn);
            delete browseConn;
            delete errorConn;
        });
        *errorConn = connect(m_librarySharing, &LibrarySharing::lastErrorChanged, this, [=]() {
            if (!m_pendingLibrarySocket) return;
            QString error = m_librarySharing->lastError();
            if (error.isEmpty()) return;
            QJsonObject resp;
            resp["error"] = error;
            sendJson(m_pendingLibrarySocket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
            m_pendingLibrarySocket = nullptr;
            disconnect(*browseConn);
            disconnect(*errorConn);
            delete browseConn;
            delete errorConn;
        });

        m_librarySharing->browseCommunity(type, QString(), QString(), search, sort, page);
        return;
    }

    // All remaining layout/library/community endpoints are POST
    if (method != "POST") {
        sendResponse(socket, 405, "application/json", R"({"error":"Method not allowed"})");
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    QJsonObject obj = doc.object();

    if (path == "/api/layout/add") {
        QString type = obj["type"].toString();
        QString zone = obj["zone"].toString();
        int index = obj.contains("index") ? obj["index"].toInt() : -1;
        if (type.isEmpty() || zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing type or zone"})");
            return;
        }
        m_settings->addItem(type, zone, index);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/remove") {
        QString itemId = obj["itemId"].toString();
        QString zone = obj["zone"].toString();
        if (itemId.isEmpty() || zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing itemId or zone"})");
            return;
        }
        m_settings->removeItem(itemId, zone);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/move") {
        QString itemId = obj["itemId"].toString();
        QString fromZone = obj["fromZone"].toString();
        QString toZone = obj["toZone"].toString();
        int toIndex = obj.contains("toIndex") ? obj["toIndex"].toInt() : -1;
        if (itemId.isEmpty() || fromZone.isEmpty() || toZone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing itemId, fromZone, or toZone"})");
            return;
        }
        m_settings->moveItem(itemId, fromZone, toZone, toIndex);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/reorder") {
        QString zone = obj["zone"].toString();
        int fromIndex = obj["fromIndex"].toInt();
        int toIndex = obj["toIndex"].toInt();
        if (zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing zone"})");
            return;
        }
        m_settings->reorderItem(zone, fromIndex, toIndex);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/reset") {
        m_settings->resetLayoutToDefault();
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/item") {
        QString itemId = obj["itemId"].toString();
        QString key = obj["key"].toString();
        if (itemId.isEmpty() || key.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing itemId or key"})");
            return;
        }
        QVariant value = obj["value"].toVariant();
        m_settings->setItemProperty(itemId, key, value);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/zone-offset") {
        QString zone = obj["zone"].toString();
        int offset = obj["offset"].toInt();
        if (zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing zone"})");
            return;
        }
        m_settings->setZoneYOffset(zone, offset);
        sendJson(socket, R"({"success":true})");
    }
    else if (path == "/api/layout/ai") {
        if (!m_aiManager) {
            sendJson(socket, R"({"error":"AI manager not available"})");
            return;
        }
        if (!m_aiManager->isConfigured()) {
            sendJson(socket, R"({"error":"No AI provider configured. Go to Settings \u2192 AI on the machine to set up a provider."})");
            return;
        }
        if (m_aiManager->isAnalyzing()) {
            sendJson(socket, R"({"error":"AI is already processing a request. Please wait."})");
            return;
        }
        QString userPrompt = obj["prompt"].toString();
        if (userPrompt.isEmpty()) {
            sendJson(socket, R"({"error":"Missing prompt"})");
            return;
        }

        // Build system prompt with layout context
        QString currentLayout = m_settings ? m_settings->layoutConfiguration() : "{}";
        QString systemPrompt = QStringLiteral(
            "You are a layout designer for the Decenza DE1 espresso machine controller app. "
            "The app has a customizable layout with these zones:\n"
            "- statusBar: Top status bar visible on ALL pages (compact horizontal bar)\n"
            "- topLeft / topRight: Top bar of home screen (compact)\n"
            "- centerStatus: Status readouts area (large widgets)\n"
            "- centerTop: Main action buttons area (large buttons)\n"
            "- centerMiddle: Info display area (large widgets)\n"
            "- bottomLeft / bottomRight: Bottom bar of home screen (compact)\n\n"
            "Available widget types:\n"
            "- espresso: Espresso button (with profile presets)\n"
            "- steam: Steam button (with pitcher presets)\n"
            "- hotwater: Hot water button (with vessel presets)\n"
            "- flush: Flush button (with flush presets)\n"
            "- beans: Bean presets button\n"
            "- history: Shot history navigation\n"
            "- autofavorites: Auto-favorites navigation\n"
            "- sleep: Put machine to sleep\n"
            "- settings: Navigate to settings\n"
            "- temperature: Group head temperature (tap to tare scale)\n"
            "- steamTemperature: Steam boiler temperature\n"
            "- waterLevel: Water tank level (ml or %)\n"
            "- connectionStatus: Machine online/offline indicator\n"
            "- scaleWeight: Scale weight with tare/ratio (tap=tare, double-tap=ratio)\n"
            "- shotPlan: Shot plan summary (profile, dose, yield)\n"
            "- pageTitle: Current page name (for status bar)\n"
            "- spacer: Flexible empty space (fills available width)\n"
            "- separator: Thin vertical line divider\n"
            "- text: Custom text with variable substitution (%TEMP%, %STEAM_TEMP%, %WEIGHT%, %PROFILE%, %TIME%, etc.)\n"
            "- weather: Weather display\n\n"
            "Each item needs a unique 'id' (format: typename + number, e.g. 'espresso1', 'temp_sb1').\n"
            "The 'offsets' object can have vertical offsets for center zones (e.g. centerStatus: -65).\n\n"
            "Current layout:\n%1\n\n"
            "Respond with ONLY the complete layout JSON (no markdown, no explanation). "
            "The JSON must have 'version':1, 'zones' object with all zone arrays, and optional 'offsets' object."
        ).arg(currentLayout);

        // Store socket for async response
        m_pendingAiSocket = socket;

        // Connect to AI signals (one-shot)
        auto onResult = [this](const QString& recommendation) {
            if (!m_pendingAiSocket) return;

            // Try to parse as JSON to validate
            QJsonDocument doc = QJsonDocument::fromJson(recommendation.toUtf8());
            if (doc.isObject() && doc.object().contains("zones")) {
                // Valid layout JSON - apply it
                if (m_settings) {
                    m_settings->setLayoutConfiguration(recommendation);
                }
                QJsonObject response;
                response["success"] = true;
                response["layout"] = doc.object();
                sendJson(m_pendingAiSocket, QJsonDocument(response).toJson(QJsonDocument::Compact));
            } else {
                // AI returned text, not valid JSON - send as suggestion
                QJsonObject response;
                response["success"] = false;
                response["message"] = recommendation;
                sendJson(m_pendingAiSocket, QJsonDocument(response).toJson(QJsonDocument::Compact));
            }
            m_pendingAiSocket = nullptr;
        };

        auto onError = [this](const QString& error) {
            if (!m_pendingAiSocket) return;
            QJsonObject response;
            response["error"] = error;
            sendJson(m_pendingAiSocket, QJsonDocument(response).toJson(QJsonDocument::Compact));
            m_pendingAiSocket = nullptr;
        };

        // One-shot connections
        QMetaObject::Connection* resultConn = new QMetaObject::Connection();
        QMetaObject::Connection* errorConn = new QMetaObject::Connection();
        *resultConn = connect(m_aiManager, &AIManager::recommendationReceived, this, [=](const QString& r) {
            onResult(r);
            disconnect(*resultConn);
            disconnect(*errorConn);
            delete resultConn;
            delete errorConn;
        });
        *errorConn = connect(m_aiManager, &AIManager::errorOccurred, this, [=](const QString& e) {
            onError(e);
            disconnect(*resultConn);
            disconnect(*errorConn);
            delete resultConn;
            delete errorConn;
        });

        m_aiManager->analyze(systemPrompt, userPrompt);
    }
    // ========== Library API (local, synchronous) ==========

    else if (method == "POST" && path == "/api/library/save-item") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QString itemId = obj["itemId"].toString();
        if (itemId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing itemId"})");
            return;
        }
        QString entryId = m_widgetLibrary->addItemFromLayout(itemId);
        if (entryId.isEmpty()) {
            sendJson(socket, R"({"error":"Failed to save item"})");
            return;
        }
        QJsonObject resp;
        resp["success"] = true;
        resp["entryId"] = entryId;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    }
    else if (method == "POST" && path == "/api/library/save-zone") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QString zone = obj["zone"].toString();
        if (zone.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing zone"})");
            return;
        }
        QString entryId = m_widgetLibrary->addZoneFromLayout(zone);
        if (entryId.isEmpty()) {
            sendJson(socket, R"({"error":"Failed to save zone"})");
            return;
        }
        QJsonObject resp;
        resp["success"] = true;
        resp["entryId"] = entryId;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    }
    else if (method == "POST" && path == "/api/library/save-layout") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QString entryId = m_widgetLibrary->addCurrentLayout(false);
        if (entryId.isEmpty()) {
            sendJson(socket, R"({"error":"Failed to save layout"})");
            return;
        }
        QJsonObject resp;
        resp["success"] = true;
        resp["entryId"] = entryId;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    }
    else if (method == "POST" && path == "/api/library/apply") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QString entryId = obj["entryId"].toString();
        QString targetZone = obj["zone"].toString();
        if (entryId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing entryId"})");
            return;
        }
        // Determine type from entry metadata
        QVariantMap meta = m_widgetLibrary->getEntry(entryId);
        QString type = meta["type"].toString();
        bool ok = false;
        if (type == "item") {
            if (targetZone.isEmpty()) {
                sendResponse(socket, 400, "application/json", R"({"error":"Missing zone for item apply"})");
                return;
            }
            ok = m_widgetLibrary->applyItem(entryId, targetZone);
        } else if (type == "zone") {
            if (targetZone.isEmpty()) {
                sendResponse(socket, 400, "application/json", R"({"error":"Missing zone for zone apply"})");
                return;
            }
            ok = m_widgetLibrary->applyZone(entryId, targetZone);
        } else if (type == "layout") {
            ok = m_widgetLibrary->applyLayout(entryId, false);
        }
        QJsonObject resp;
        resp["success"] = ok;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    }
    else if (method == "POST" && path == "/api/library/delete") {
        if (!m_widgetLibrary) {
            sendJson(socket, R"({"error":"Library not available"})");
            return;
        }
        QString entryId = obj["entryId"].toString();
        if (entryId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing entryId"})");
            return;
        }
        bool ok = m_widgetLibrary->removeEntry(entryId);
        QJsonObject resp;
        resp["success"] = ok;
        sendJson(socket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
    }

    // ========== Community API (async, signal-based) ==========

    else if (method == "POST" && path == "/api/community/download") {
        if (!m_librarySharing) {
            sendJson(socket, R"({"error":"Community sharing not available"})");
            return;
        }
        QString serverId = obj["serverId"].toString();
        if (serverId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing serverId"})");
            return;
        }

        m_pendingLibrarySocket = socket;

        QMetaObject::Connection* doneConn = new QMetaObject::Connection();
        QMetaObject::Connection* failConn = new QMetaObject::Connection();

        *doneConn = connect(m_librarySharing, &LibrarySharing::downloadComplete, this, [=](const QString& localEntryId) {
            if (!m_pendingLibrarySocket) return;
            QJsonObject resp;
            resp["success"] = true;
            resp["localEntryId"] = localEntryId;
            sendJson(m_pendingLibrarySocket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
            m_pendingLibrarySocket = nullptr;
            disconnect(*doneConn);
            disconnect(*failConn);
            delete doneConn;
            delete failConn;
        });
        *failConn = connect(m_librarySharing, &LibrarySharing::downloadFailed, this, [=](const QString& error) {
            if (!m_pendingLibrarySocket) return;
            QJsonObject resp;
            resp["error"] = error;
            sendJson(m_pendingLibrarySocket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
            m_pendingLibrarySocket = nullptr;
            disconnect(*doneConn);
            disconnect(*failConn);
            delete doneConn;
            delete failConn;
        });

        m_librarySharing->downloadEntry(serverId);
    }
    else if (method == "POST" && path == "/api/community/upload") {
        if (!m_librarySharing) {
            sendJson(socket, R"({"error":"Community sharing not available"})");
            return;
        }
        QString entryId = obj["entryId"].toString();
        if (entryId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing entryId"})");
            return;
        }

        m_pendingLibrarySocket = socket;

        QMetaObject::Connection* successConn = new QMetaObject::Connection();
        QMetaObject::Connection* failConn = new QMetaObject::Connection();

        *successConn = connect(m_librarySharing, &LibrarySharing::uploadSuccess, this, [=](const QString& serverId) {
            if (!m_pendingLibrarySocket) return;
            QJsonObject resp;
            resp["success"] = true;
            resp["serverId"] = serverId;
            sendJson(m_pendingLibrarySocket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
            m_pendingLibrarySocket = nullptr;
            disconnect(*successConn);
            disconnect(*failConn);
            delete successConn;
            delete failConn;
        });
        *failConn = connect(m_librarySharing, &LibrarySharing::uploadFailed, this, [=](const QString& error) {
            if (!m_pendingLibrarySocket) return;
            QJsonObject resp;
            resp["error"] = error;
            sendJson(m_pendingLibrarySocket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
            m_pendingLibrarySocket = nullptr;
            disconnect(*successConn);
            disconnect(*failConn);
            delete successConn;
            delete failConn;
        });

        m_librarySharing->uploadEntry(entryId);
    }
    else if (method == "POST" && path == "/api/community/delete") {
        if (!m_librarySharing) {
            sendJson(socket, R"({"error":"Community sharing not available"})");
            return;
        }
        QString serverId = obj["serverId"].toString();
        if (serverId.isEmpty()) {
            sendResponse(socket, 400, "application/json", R"({"error":"Missing serverId"})");
            return;
        }

        m_pendingLibrarySocket = socket;

        QMetaObject::Connection* successConn = new QMetaObject::Connection();
        QMetaObject::Connection* failConn = new QMetaObject::Connection();

        *successConn = connect(m_librarySharing, &LibrarySharing::deleteSuccess, this, [=]() {
            if (!m_pendingLibrarySocket) return;
            QJsonObject resp;
            resp["success"] = true;
            sendJson(m_pendingLibrarySocket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
            m_pendingLibrarySocket = nullptr;
            disconnect(*successConn);
            disconnect(*failConn);
            delete successConn;
            delete failConn;
        });
        *failConn = connect(m_librarySharing, &LibrarySharing::deleteFailed, this, [=](const QString& error) {
            if (!m_pendingLibrarySocket) return;
            QJsonObject resp;
            resp["error"] = error;
            sendJson(m_pendingLibrarySocket, QJsonDocument(resp).toJson(QJsonDocument::Compact));
            m_pendingLibrarySocket = nullptr;
            disconnect(*successConn);
            disconnect(*failConn);
            delete successConn;
            delete failConn;
        });

        m_librarySharing->deleteFromServer(serverId);
    }

    else {
        sendResponse(socket, 404, "application/json", R"({"error":"Unknown layout endpoint"})");
    }
}

QString ShotServer::generateLayoutPage() const
{
    QString html;

    // Part 1: Head and base CSS
    html += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Layout Editor - Decenza DE1</title>
    <style>
)HTML";
    html += WEB_CSS_VARIABLES;
    html += WEB_CSS_HEADER;
    html += WEB_CSS_MENU;

    // Part 2: Page-specific CSS
    html += R"HTML(
        .main-layout {
            display: flex;
            flex-direction: column;
            gap: 1.5rem;
            max-width: 1400px;
            margin: 0 auto;
            padding: 1.5rem;
        }
        .zones-panel { min-width: 0; }
        .editor-panel { }
        .zone-card {
            background: var(--surface);
            border: 2px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1rem;
            transition: border-color 0.15s;
        }
        .zone-card.selected { border-color: var(--accent); }
        .zone-header {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 0.75rem;
        }
        .zone-title {
            color: var(--text-secondary);
            font-size: 0.8rem;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .zone-row { display: flex; gap: 0.5rem; }
        .zone-offset-controls { display: flex; gap: 0.25rem; align-items: center; }
        .offset-btn {
            background: none;
            border: 1px solid var(--border);
            color: var(--accent);
            width: 28px;
            height: 28px;
            border-radius: 6px;
            cursor: pointer;
            font-size: 0.75rem;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .offset-btn:hover { background: var(--surface-hover); }
        .offset-val {
            color: var(--text-secondary);
            font-size: 0.75rem;
            min-width: 2rem;
            text-align: center;
        }
        .chips-area {
            display: flex;
            flex-wrap: wrap;
            gap: 0.5rem;
            align-items: center;
            min-height: 40px;
        }
        .chip {
            display: inline-flex;
            align-items: center;
            gap: 0.25rem;
            padding: 0.375rem 0.75rem;
            border-radius: 8px;
            background: var(--bg);
            border: 1px solid var(--border);
            color: var(--text);
            cursor: pointer;
            font-size: 0.875rem;
            user-select: none;
            transition: all 0.15s;
        }
        .chip:hover { border-color: var(--accent); }
        .chip.selected {
            border: 2px solid var(--accent);
        }
        .chip.special { color: orange; }
        .chip.selected.special { color: orange; }
        .chip-arrow {
            cursor: pointer;
            font-size: 1rem;
            opacity: 0.8;
        }
        .chip-arrow:hover { opacity: 1; }
)HTML";
    html += R"HTML(
        .chip-remove {
            cursor: pointer;
            color: #f85149;
            font-weight: bold;
            font-size: 1rem;
            margin-left: 0.25rem;
        }
        .add-btn {
            width: 36px;
            height: 36px;
            border-radius: 8px;
            background: none;
            border: 1px solid var(--accent);
            color: var(--accent);
            font-size: 1.25rem;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            position: relative;
        }
        .add-btn:hover { background: rgba(201,162,39,0.1); }
        .add-dropdown {
            display: none;
            position: absolute;
            top: 100%;
            left: 0;
            margin-top: 0.25rem;
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0,0,0,0.3);
            z-index: 50;
            min-width: 160px;
            max-height: 400px;
            overflow-y: auto;
        }
        .add-dropdown.open { display: block; }
        .add-dropdown-item {
            display: block;
            padding: 0.5rem 0.75rem;
            color: var(--text);
            cursor: pointer;
            font-size: 0.875rem;
            white-space: nowrap;
        }
        .add-dropdown-item:hover { background: var(--surface-hover); }
        .add-dropdown-item.special { color: orange; }
        .reset-btn {
            background: none;
            border: 1px solid var(--border);
            color: var(--text-secondary);
            padding: 0.375rem 0.75rem;
            border-radius: 6px;
            cursor: pointer;
            font-size: 0.8rem;
        }
        .reset-btn:hover { color: var(--accent); border-color: var(--accent); }

        /* AI dialog */
        .ai-overlay {
            display: none;
            position: fixed;
            inset: 0;
            background: rgba(0,0,0,0.6);
            z-index: 100;
            align-items: center;
            justify-content: center;
        }
        .ai-overlay.open { display: flex; }
)HTML";

    // Part 2b: AI dialog and remaining CSS
    html += R"HTML(
        .ai-dialog {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.5rem;
            width: min(90vw, 540px);
            max-height: 80vh;
            overflow-y: auto;
        }
        .ai-dialog h3 { color: var(--accent); margin: 0 0 1rem; font-size: 1rem; }
        .ai-prompt {
            width: 100%;
            min-height: 80px;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--text);
            font-size: 0.875rem;
            padding: 0.75rem;
            resize: vertical;
            box-sizing: border-box;
        }
        .ai-prompt:focus { border-color: var(--accent); outline: none; }
        .ai-result {
            margin-top: 0.75rem;
            padding: 0.75rem;
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            font-size: 0.85rem;
            color: var(--text);
            white-space: pre-wrap;
            max-height: 200px;
            overflow-y: auto;
        }
        .ai-result.error { border-color: #f85149; color: #f85149; }
        .ai-result.success { border-color: var(--accent); }
        .ai-loading { color: var(--text-secondary); font-style: italic; }
        .ai-btns { display: flex; gap: 0.5rem; justify-content: flex-end; margin-top: 0.75rem; }

        /* Text editor panel */
        .editor-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.25rem;
        }
        .editor-card h3 {
            font-size: 0.9rem;
            margin-bottom: 1rem;
            color: var(--accent);
        }
        .editor-hidden { display: none; }
        .toolbar {
            display: flex;
            flex-wrap: wrap;
            gap: 0.25rem;
            margin: 0.75rem 0;
        }
        .tool-btn {
            width: 32px;
            height: 32px;
            border-radius: 4px;
            background: var(--bg);
            border: 1px solid var(--border);
            color: var(--text);
            cursor: pointer;
            font-size: 0.8rem;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .tool-btn:hover { border-color: var(--accent); }
        .tool-btn.active { background: var(--accent); color: #000; border-color: var(--accent); }
        .tool-sep { width: 1px; height: 24px; background: var(--border); align-self: center; margin: 0 0.25rem; }
        .section-label {
            font-size: 0.75rem;
            color: var(--text-secondary);
            margin: 0.5rem 0 0.25rem;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .var-list {
            max-height: 180px;
            overflow-y: auto;
            border: 1px solid var(--border);
            border-radius: 6px;
            background: var(--bg);
        }
        .var-item {
            padding: 0.375rem 0.5rem;
            cursor: pointer;
            font-size: 0.8rem;
            color: var(--accent);
            border-bottom: 1px solid var(--border);
        }
        .var-item:last-child { border-bottom: none; }
        .var-item:hover { background: var(--surface-hover); }
        .editor-buttons {
            display: flex;
            gap: 0.5rem;
            justify-content: flex-end;
            margin-top: 0.75rem;
        }
        .btn {
            padding: 0.5rem 1rem;
            border-radius: 6px;
            cursor: pointer;
            font-size: 0.875rem;
            border: 1px solid var(--border);
        }
        .btn-cancel { background: var(--bg); color: var(--text); }
        .btn-cancel:hover { border-color: var(--accent); }
        .btn-save { background: var(--accent); color: #000; border-color: var(--accent); font-weight: 600; }
        .btn-save:hover { background: var(--accent-dim); }
)HTML";
    html += R"HTML(
        /* --- WYSIWYG Editor (matching tablet design) --- */
        .wysiwyg-editor {
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 0.5rem;
            min-height: 100px;
            max-height: 200px;
            overflow-y: auto;
            color: var(--text);
            font-size: 0.9rem;
            outline: none;
            white-space: pre-wrap;
            word-wrap: break-word;
        }
        .wysiwyg-editor:focus { border-color: var(--accent); }
        .wysiwyg-editor:empty::before {
            content: "Enter text...";
            color: var(--text-secondary);
            pointer-events: none;
        }

        /* Row 1: Icon/Emoji */
        .editor-icon-row {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            margin-bottom: 0.5rem;
        }
        .icon-preview {
            width: 40px;
            height: 40px;
            border-radius: 6px;
            background: var(--bg);
            border: 1px solid var(--border);
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 1.5rem;
        }
        .icon-preview img { width: 28px; height: 28px; filter: brightness(0) invert(1); }
        .icon-btn {
            padding: 4px 10px;
            border-radius: 6px;
            cursor: pointer;
            font-size: 0.75rem;
            border: 1px solid var(--accent);
            background: none;
            color: var(--accent);
        }
        .icon-btn:hover { background: rgba(201,162,39,0.1); }
        .icon-btn.danger { border-color: #f85149; color: #f85149; }
        .icon-btn.danger:hover { background: rgba(248,81,73,0.1); }
        .emoji-picker-area { margin-bottom: 0.5rem; }
        .emoji-tabs {
            display: flex;
            gap: 2px;
            margin-bottom: 4px;
            flex-wrap: wrap;
        }
        .emoji-tab {
            padding: 2px 8px;
            border-radius: 4px;
            background: var(--bg);
            border: 1px solid var(--border);
            color: var(--text-secondary);
            cursor: pointer;
            font-size: 0.7rem;
        }
        .emoji-tab:hover { border-color: var(--accent); }
        .emoji-tab.active { background: var(--accent); color: #000; border-color: var(--accent); }
        .emoji-grid {
            display: flex;
            flex-wrap: wrap;
            gap: 2px;
            max-height: 140px;
            overflow-y: auto;
            border: 1px solid var(--border);
            border-radius: 6px;
            padding: 4px;
            background: var(--bg);
        }
        .emoji-cell {
            width: 36px;
            height: 36px;
            display: flex;
            align-items: center;
            justify-content: center;
            border-radius: 4px;
            cursor: pointer;
            font-size: 1.25rem;
        }
        .emoji-cell:hover { background: var(--surface-hover); }
        .emoji-cell.selected { background: var(--accent); border-radius: 6px; }
        .emoji-cell img { width: 24px; height: 24px; filter: brightness(0) invert(1); }
)HTML";
    html += R"HTML(
        /* Row 2: Content + Preview */
        .editor-content-row {
            display: flex;
            gap: 0.75rem;
            margin-bottom: 0.5rem;
        }
        .editor-content-col { flex: 1; min-width: 0; }
        .editor-preview-col {
            flex: 0 0 auto;
            display: flex;
            flex-direction: column;
            gap: 4px;
        }
        .preview-label {
            font-size: 0.65rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .preview-full {
            min-width: 120px;
            height: 80px;
            border-radius: 8px;
            background: var(--bg);
            border: 1px solid var(--border);
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            gap: 2px;
            overflow: hidden;
            padding: 4px;
            color: var(--text);
            font-size: 0.75rem;
            text-align: center;
        }
        .preview-full.has-action { border-color: var(--accent); border-width: 2px; }
        .preview-full img { width: 28px; height: 28px; filter: brightness(0) invert(1); }
        .preview-full .pv-emoji { font-size: 1.5rem; }
        .preview-full .pv-text {
            max-width: 100%;
            overflow: hidden;
            text-overflow: ellipsis;
            display: -webkit-box;
            -webkit-line-clamp: 2;
            -webkit-box-orient: vertical;
        }
        .preview-bar {
            min-width: 120px;
            height: 32px;
            border-radius: 8px;
            background: var(--bg);
            border: 1px solid var(--border);
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 4px;
            overflow: hidden;
            padding: 0 6px;
            color: var(--text);
            font-size: 0.75rem;
            white-space: nowrap;
        }
        .preview-bar.has-action { border-color: var(--accent); border-width: 2px; }
        .preview-bar img { width: 18px; height: 18px; filter: brightness(0) invert(1); }
        .preview-bar .pv-emoji { font-size: 1rem; }
        .preview-bar .pv-text { overflow: hidden; text-overflow: ellipsis; }
)HTML";
    html += R"HTML(
        /* Row 3: Format | Variables | Actions */
        .editor-tools-row {
            display: flex;
            gap: 0.75rem;
            min-height: 0;
        }
        .editor-tools-format {
            flex: 0 0 auto;
        }
        .editor-tools-vars {
            flex: 0 0 180px;
            min-width: 0;
            display: flex;
            flex-direction: column;
        }
        .editor-tools-vars .var-list { flex: 1; min-height: 0; max-height: 200px; }
        .editor-tools-actions {
            flex: 0 0 auto;
            min-width: 140px;
            max-width: 200px;
            display: flex;
            flex-direction: column;
            gap: 4px;
        }
        .action-selector {
            padding: 6px 8px;
            border-radius: 6px;
            background: var(--bg);
            border: 1px solid var(--border);
            cursor: pointer;
            font-size: 0.75rem;
            display: flex;
            gap: 4px;
            align-items: center;
        }
        .action-selector:hover { border-color: var(--accent); }
        .action-selector.has-action { border-color: var(--accent); }
        .action-selector .action-label-prefix {
            color: var(--text-secondary);
            flex: 0 0 auto;
        }
        .action-selector .action-label-value {
            color: var(--text);
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }
        .action-selector.has-action .action-label-value { color: var(--accent); }
        .color-row {
            display: flex;
            align-items: center;
            gap: 6px;
            flex-wrap: wrap;
        }
)HTML";
    html += R"HTML(
        .color-swatch {
            width: 26px;
            height: 26px;
            border-radius: 50%;
            border: 1px solid var(--border);
            cursor: pointer;
            position: relative;
        }
        .color-swatch:hover { border-color: white; }
        .color-swatch.active { border-color: white; border-width: 2px; }
        .color-swatch-x {
            width: 22px;
            height: 22px;
            border-radius: 50%;
            border: 1px solid #f85149;
            cursor: pointer;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 0.7rem;
            color: #f85149;
            background: none;
        }
        .color-swatch-x:hover { background: rgba(248,81,73,0.15); }
)HTML";
    html += R"HTML(
        .color-popup-overlay {
            display: none;
            position: fixed;
            top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(0,0,0,0.5);
            z-index: 2000;
            align-items: center;
            justify-content: center;
        }
        .color-popup-overlay.open { display: flex; }
        .color-popup {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            min-width: 300px;
            max-width: 400px;
        }
        .color-popup h4 {
            color: var(--text);
            margin: -0.5rem -0.5rem 0.75rem;
            padding: 0.5rem;
            font-size: 0.85rem;
            text-align: center;
            cursor: move;
            user-select: none;
            border-bottom: 1px solid var(--border);
            border-radius: 12px 12px 0 0;
        }
        .color-popup h4:hover { background: var(--surface-hover); }
        .color-popup-group {
            margin-bottom: 0.5rem;
        }
        .color-popup-group-label {
            font-size: 0.65rem;
            color: var(--text-secondary);
            margin-bottom: 4px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        .color-popup-grid {
            display: flex;
            gap: 6px;
            flex-wrap: wrap;
        }
        .cp-swatch {
            width: 28px;
            height: 28px;
            border-radius: 50%;
            border: 2px solid transparent;
            cursor: pointer;
            transition: transform 0.1s, border-color 0.1s;
            position: relative;
        }
        .cp-swatch:hover { border-color: white; transform: scale(1.15); }
        .cp-swatch[title]:hover::after {
            content: attr(title);
            position: absolute;
            bottom: -20px;
            left: 50%;
            transform: translateX(-50%);
            font-size: 0.6rem;
            color: var(--text-secondary);
            white-space: nowrap;
            pointer-events: none;
        }
)HTML";
    html += R"HTML(
        .color-popup-footer {
            display: flex;
            gap: 6px;
            margin-top: 0.75rem;
            justify-content: flex-end;
        }
        .color-popup-footer button {
            padding: 0.3rem 0.75rem;
            border-radius: 6px;
            border: 1px solid var(--border);
            background: var(--bg);
            color: var(--text);
            cursor: pointer;
            font-size: 0.75rem;
        }
        .color-popup-footer button:hover { border-color: var(--accent); }
        .color-popup-footer .cp-custom-btn { border-color: var(--accent); color: var(--accent); }
        .color-popup-footer .cp-apply-btn { background: var(--accent); color: #000; border-color: var(--accent); font-weight: 600; }
        .color-popup-footer .cp-apply-btn:hover { filter: brightness(1.1); }
        .cp-picker-row {
            display: flex;
            align-items: center;
            gap: 12px;
            margin-top: 0.5rem;
            padding-top: 0.5rem;
            border-top: 1px solid var(--border);
        }
        .cp-picker-row #iroPickerContainer { flex-shrink: 0; }
        .cp-picker-right { display: flex; flex-direction: column; gap: 8px; flex: 1; }
        .cp-hex-row { display: flex; align-items: center; gap: 6px; }
        .cp-hex-row label { font-size: 0.7rem; color: var(--text-secondary); }
        .cp-hex-input {
            width: 80px;
            padding: 4px 6px;
            border: 1px solid var(--border);
            border-radius: 4px;
            background: var(--bg);
            color: var(--text);
            font-family: monospace;
            font-size: 0.8rem;
        }
        .cp-hex-input:focus { border-color: var(--accent); outline: none; }
        .cp-preview-swatch {
            width: 36px; height: 36px;
            border-radius: 6px;
            border: 2px solid var(--border);
        }
        .color-label {
            font-size: 0.75rem;
            color: var(--text-secondary);
        }

        /* Action picker overlay */
        .action-overlay {
            display: none;
            position: fixed;
            inset: 0;
            background: rgba(0,0,0,0.5);
            z-index: 200;
            align-items: center;
            justify-content: center;
        }
        .action-overlay.open { display: flex; }
        .action-dialog {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            width: min(80vw, 280px);
            max-height: 400px;
            overflow-y: auto;
        }
        .action-dialog h4 { color: var(--text); margin: 0 0 0.5rem; font-size: 0.9rem; text-align: center; }
        .action-dialog-item {
            padding: 0.4rem 0.6rem;
            cursor: pointer;
            font-size: 0.8rem;
            color: var(--text);
            border-radius: 4px;
        }
        .action-dialog-item:hover { background: var(--surface-hover); }
        .action-dialog-item.selected { background: var(--accent); color: #000; }

        /* Convert button & chip styling */
        .convert-btn {
            background: none;
            border: 1px solid orange;
            color: orange;
            padding: 0.25rem 0.5rem;
            border-radius: 4px;
            cursor: pointer;
            font-size: 0.75rem;
            margin-left: 0.5rem;
        }
        .convert-btn:hover { background: rgba(255,165,0,0.1); }
        .chip-emoji { margin-right: 2px; font-size: 0.8rem; }
        .chip-emoji img { width: 14px; height: 14px; vertical-align: middle; filter: brightness(0) invert(1); }

        @media (max-width: 700px) {
            .editor-content-row { flex-direction: column; }
            .editor-tools-row { flex-direction: column; }
            .editor-preview-col { flex-direction: row; gap: 0.5rem; }
        }

        /* Library panel */
        .main-wrapper {
            display: flex;
            gap: 0;
            max-width: 1800px;
            margin: 0 auto;
        }
        .main-wrapper .main-layout { flex: 1; min-width: 0; }
        .library-panel {
            width: 340px;
            min-width: 340px;
            background: var(--surface);
            border-left: 1px solid var(--border);
            padding: 1rem;
            display: flex;
            flex-direction: column;
            height: calc(100vh - 60px);
            overflow-y: auto;
            position: sticky;
            top: 60px;
        }
        .lib-tabs {
            display: flex;
            gap: 0;
            margin-bottom: 1rem;
            border-radius: 8px;
            overflow: hidden;
            border: 1px solid var(--border);
        }
        .lib-tab {
            flex: 1;
            padding: 0.5rem;
            text-align: center;
            cursor: pointer;
            background: var(--bg);
            color: var(--text-secondary);
            font-size: 0.8rem;
            font-weight: 600;
            border: none;
            transition: all 0.15s;
        }
        .lib-tab.active {
            background: var(--accent);
            color: #fff;
        }
        .lib-tab:hover:not(.active) { background: var(--surface-hover); }
        .lib-actions {
            display: flex;
            gap: 0.5rem;
            margin-bottom: 1rem;
            flex-wrap: wrap;
        }
        .lib-action-btn {
            padding: 0.35rem 0.6rem;
            border-radius: 6px;
            border: 1px solid var(--border);
            background: var(--bg);
            color: var(--text);
            cursor: pointer;
            font-size: 0.75rem;
            transition: all 0.15s;
        }
        .lib-action-btn:hover { border-color: var(--accent); color: var(--accent); }
        .lib-action-btn.accent { border-color: var(--accent); color: var(--accent); }
        .lib-action-btn:disabled { opacity: 0.4; cursor: default; }
        .lib-entries {
            flex: 1;
            overflow-y: auto;
            display: flex;
            flex-direction: column;
            gap: 0.5rem;
        }
        .lib-entry {
            background: var(--bg);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 0.6rem;
            cursor: pointer;
            transition: all 0.15s;
        }
        .lib-entry:hover { border-color: var(--accent); }
        .lib-entry.selected { border: 2px solid var(--accent); }
        .lib-entry-visual {
            border-radius: 6px;
            padding: 0.4rem 0.6rem;
            display: flex;
            align-items: center;
            gap: 0.4rem;
            min-height: 32px;
            position: relative;
            overflow: hidden;
        }
        .lib-entry-visual img.lib-thumb {
            width: 100%;
            height: 48px;
            object-fit: contain;
            border-radius: 4px;
        }
        .lib-entry-visual .lib-item-emoji { width: 22px; height: 22px; flex-shrink: 0; }
        .lib-entry-visual .lib-item-emoji img { width: 100%; height: 100%; filter: brightness(0) invert(1); }
        .lib-entry-visual .lib-item-text {
            color: white;
            font-size: 0.8rem;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        }
        .lib-entry-visual .lib-zone-chips {
            display: flex;
            flex-wrap: wrap;
            gap: 3px;
        }
        .lib-zone-mini-chip {
            display: inline-flex;
            align-items: center;
            gap: 2px;
            padding: 1px 6px;
            border-radius: 4px;
            font-size: 0.65rem;
            color: white;
            white-space: nowrap;
        }
        .lib-zone-mini-chip img { width: 12px; height: 12px; filter: brightness(0) invert(1); }
        .lib-type-overlay {
            position: absolute;
            top: 3px;
            left: 3px;
            font-size: 0.55rem;
            font-weight: 700;
            text-transform: uppercase;
            padding: 1px 4px;
            border-radius: 3px;
            opacity: 0.85;
            letter-spacing: 0.03em;
        }
        .lib-type-overlay.item { background: #1a3a5c; color: #4e85f4; }
        .lib-type-overlay.zone { background: #3a3520; color: #c9a227; }
        .lib-type-overlay.layout { background: #1a3a2a; color: #00cc6d; }
        .lib-empty {
            color: var(--text-secondary);
            text-align: center;
            padding: 2rem 1rem;
            font-size: 0.85rem;
        }
        .lib-community-filters {
            display: flex;
            gap: 0.5rem;
            margin-bottom: 0.75rem;
            flex-wrap: wrap;
        }
        .lib-filter-select, .lib-filter-input {
            padding: 0.35rem 0.5rem;
            border-radius: 6px;
            border: 1px solid var(--border);
            background: var(--bg);
            color: var(--text);
            font-size: 0.75rem;
            flex: 1;
            min-width: 0;
        }
        .lib-filter-input { flex: 2; }
        .lib-load-more {
            padding: 0.5rem;
            text-align: center;
            color: var(--accent);
            cursor: pointer;
            font-size: 0.8rem;
            border: 1px dashed var(--border);
            border-radius: 6px;
            margin-top: 0.5rem;
        }
        .lib-load-more:hover { background: var(--surface-hover); }
        .lib-toast {
            position: fixed;
            bottom: 1.5rem;
            left: 50%;
            transform: translateX(-50%);
            background: var(--accent);
            color: #fff;
            padding: 0.6rem 1.2rem;
            border-radius: 8px;
            font-size: 0.85rem;
            z-index: 9999;
            opacity: 0;
            transition: opacity 0.3s;
            pointer-events: none;
        }
        .lib-toast.show { opacity: 1; }
        .lib-spinner-overlay {
            display: none;
            position: absolute;
            inset: 0;
            background: rgba(0,0,0,0.5);
            z-index: 100;
            align-items: center;
            justify-content: center;
            flex-direction: column;
            gap: 0.8rem;
            border-radius: 8px;
        }
        .lib-spinner-overlay.active { display: flex; }
        .lib-spinner {
            width: 32px; height: 32px;
            border: 3px solid rgba(255,255,255,0.2);
            border-top-color: var(--accent);
            border-radius: 50%;
            animation: spin 0.8s linear infinite;
        }
        @keyframes spin { to { transform: rotate(360deg); } }
        .lib-spinner-text { color: #fff; font-size: 0.8rem; }
    </style>
    <script src="https://cdn.jsdelivr.net/npm/@jaames/iro@5"></script>
</head>
<body>
)HTML";

    // Part 3: Header
    html += R"HTML(
    <header class="header">
        <div class="header-content">
            <div style="display:flex;align-items:center;gap:1rem">
                <a href="/" class="back-btn">&larr;</a>
                <h1>Layout Editor</h1>
            </div>
            <div class="header-right">
                <button class="reset-btn" onclick="openAiDialog()" style="border-color:var(--accent);color:var(--accent)">&#10024; Ask AI</button>
                <button class="reset-btn" onclick="resetLayout()">Reset to Default</button>
)HTML";
    html += generateMenuHtml();
    html += R"HTML(
            </div>
        </div>
    </header>
)HTML";

    // Part 4: Main content
    html += R"HTML(
    <!-- AI Dialog -->
    <div class="ai-overlay" id="aiOverlay" onclick="if(event.target===this)closeAiDialog()">
        <div class="ai-dialog">
            <h3>&#10024; Ask AI to Design Your Layout</h3>
            <textarea class="ai-prompt" id="aiPrompt" placeholder="Describe what you want, e.g.&#10;&#10;&bull; Add steam temperature to the status bar&#10;&bull; Minimalist layout with just espresso and steam&#10;&bull; Put the clock in the top right corner&#10;&bull; Move settings to the status bar"></textarea>
            <div id="aiResultArea"></div>
            <div class="ai-btns">
                <button class="btn btn-cancel" onclick="closeAiDialog()">Close</button>
                <button class="btn btn-save" id="aiSendBtn" onclick="sendAiPrompt()">Generate</button>
            </div>
        </div>
    </div>

    <!-- Action Picker Overlay -->
    <div class="action-overlay" id="actionOverlay" onclick="if(event.target===this)closeActionPicker()">
        <div class="action-dialog">
            <h4 id="actionPickerTitle">Tap Action</h4>
            <div id="actionPickerList"></div>
        </div>
    </div>

    <div class="main-wrapper">
    <div class="main-layout">
        <div class="zones-panel" id="zonesPanel"></div>
        <div class="editor-panel editor-hidden" id="editorPanel">
            <div class="editor-card">
                <h3>Edit Custom Widget</h3>

                <!-- ROW 1: Icon/Emoji selector -->
                <div class="editor-icon-row">
                    <span class="section-label" style="margin:0">Icon</span>
                    <div class="icon-preview" id="iconPreview"><span style="color:var(--text-secondary)">&#8212;</span></div>
                    <button class="icon-btn" id="emojiToggleBtn" onclick="toggleEmojiPicker()">Pick Icon</button>
                    <button class="icon-btn danger" id="emojiClearBtn" onclick="clearEmoji()" style="display:none">Clear</button>
                </div>
                <div class="emoji-picker-area" id="emojiPickerArea" style="display:none">
                    <div class="emoji-tabs" id="emojiTabs"></div>
                    <div class="emoji-grid" id="emojiGrid"></div>
                </div>
)HTML";
    html += R"HTML(
                <!-- ROW 2: WYSIWYG Content + Dual Preview -->
                <div class="editor-content-row">
                    <div class="editor-content-col">
                        <div class="section-label">Content</div>
                        <div contenteditable="true" id="wysiwygEditor" class="wysiwyg-editor"></div>
                        <div class="toolbar" style="margin-top:0.375rem">
                            <button class="tool-btn" onclick="execBold()" title="Bold"><b>B</b></button>
                            <button class="tool-btn" onclick="execItalic()" title="Italic"><i>I</i></button>
                            <div class="tool-sep"></div>
                            <button class="tool-btn" onclick="execFontSize(12)" title="Small">S</button>
                            <button class="tool-btn" onclick="execFontSize(18)" title="Medium">M</button>
                            <button class="tool-btn" onclick="execFontSize(28)" title="Large">L</button>
                            <button class="tool-btn" onclick="execFontSize(48)" title="XL">XL</button>
                            <div class="tool-sep"></div>
                            <button class="tool-btn" id="alignLeft" onclick="setAlign('left')" title="Left">&#9664;</button>
                            <button class="tool-btn active" id="alignCenter" onclick="setAlign('center')" title="Center">&#9679;</button>
                            <button class="tool-btn" id="alignRight" onclick="setAlign('right')" title="Right">&#9654;</button>
                        </div>
                    </div>
                    <div class="editor-preview-col">
                        <span class="preview-label">Full</span>
                        <div class="preview-full" id="previewFull"></div>
                        <span class="preview-label">Bar</span>
                        <div class="preview-bar" id="previewBar"></div>
                    </div>
                </div>
)HTML";
    html += R"HTML(
                <!-- ROW 3: Format/Color | Variables | Actions -->
                <div class="editor-tools-row">
                    <div class="editor-tools-format">
                        <div class="color-row">
                            <span class="color-label">Color</span>
                            <div class="color-swatch" id="textColorSwatch" style="background:#ffffff" onclick="openColorPopup('text')"></div>
                            <input type="color" id="textColorInput" value="#ffffff" style="position:absolute;visibility:hidden;width:0;height:0" onchange="applyTextColor(this.value)">
                            <span class="color-swatch-x" onclick="clearTextColor()" title="Reset text color">&#10005;</span>
                            <span class="color-label" style="margin-left:6px">Bg</span>
                            <div class="color-swatch" id="bgColorSwatch" style="background:transparent" onclick="openColorPopup('bg')">
                                <span id="bgNoneX" style="color:var(--text-secondary);font-size:0.6rem">&#10005;</span>
                            </div>
                            <input type="color" id="bgColorInput" value="#555555" style="position:absolute;visibility:hidden;width:0;height:0" onchange="setBgColor(this.value)">
                            <span class="color-swatch-x" id="bgClearBtn" onclick="clearBgColor()" title="Remove background" style="display:none">&#10005;</span>
                        </div>
                    </div>
                    <div class="editor-tools-vars">
                        <div class="section-label" style="margin-top:0">Variables</div>
                        <div class="var-list">
                            <div class="var-item" onclick="insertVar('%TEMP%')">Temp (&deg;C)</div>
                            <div class="var-item" onclick="insertVar('%STEAM_TEMP%')">Steam (&deg;C)</div>
                            <div class="var-item" onclick="insertVar('%PRESSURE%')">Pressure</div>
                            <div class="var-item" onclick="insertVar('%FLOW%')">Flow</div>
                            <div class="var-item" onclick="insertVar('%WATER%')">Water %</div>
                            <div class="var-item" onclick="insertVar('%WATER_ML%')">Water ml</div>
                            <div class="var-item" onclick="insertVar('%WEIGHT%')">Weight</div>
                            <div class="var-item" onclick="insertVar('%SHOT_TIME%')">Shot Time</div>
                            <div class="var-item" onclick="insertVar('%TARGET_WEIGHT%')">Target Wt</div>
                            <div class="var-item" onclick="insertVar('%VOLUME%')">Volume</div>
                            <div class="var-item" onclick="insertVar('%PROFILE%')">Profile</div>
                            <div class="var-item" onclick="insertVar('%STATE%')">State</div>
                            <div class="var-item" onclick="insertVar('%TARGET_TEMP%')">Tgt Temp</div>
                            <div class="var-item" onclick="insertVar('%SCALE%')">Scale</div>
                            <div class="var-item" onclick="insertVar('%TIME%')">Time</div>
                            <div class="var-item" onclick="insertVar('%DATE%')">Date</div>
                            <div class="var-item" onclick="insertVar('%RATIO%')">Ratio</div>
                            <div class="var-item" onclick="insertVar('%DOSE%')">Dose</div>
                            <div class="var-item" onclick="insertVar('%CONNECTED%')">Online</div>
                            <div class="var-item" onclick="insertVar('%CONNECTED_COLOR%')">Status Clr</div>
                            <div class="var-item" onclick="insertVar('%DEVICES%')">Devices</div>
                        </div>
                    </div>
                    <div class="editor-tools-actions">
                        <div class="section-label" style="margin-top:0">Actions</div>
                        <div class="action-selector" id="tapActionSel" onclick="openActionPicker('click')">
                            <span class="action-label-prefix">Click:</span>
                            <span class="action-label-value" id="tapActionLabel">None</span>
                        </div>
                        <div class="action-selector" id="longPressActionSel" onclick="openActionPicker('longpress')">
                            <span class="action-label-prefix">Long:</span>
                            <span class="action-label-value" id="longPressActionLabel">None</span>
                        </div>
                        <div class="action-selector" id="dblClickActionSel" onclick="openActionPicker('doubleclick')">
                            <span class="action-label-prefix">DblClk:</span>
                            <span class="action-label-value" id="dblClickActionLabel">None</span>
                        </div>
                    </div>
                </div>

                <!-- ROW 4: Buttons -->
                <div class="editor-buttons">
                    <button class="btn btn-cancel" style="border-color:var(--accent);color:var(--accent)" onclick="openAiDialog()">&#10024; Ask AI</button>
                    <div style="flex:1"></div>
                    <button class="btn btn-cancel" onclick="closeEditor()">Done</button>
                </div>
            </div>
        </div>
    </div>

    <!-- Library Panel (right sidebar) -->
    <div class="library-panel" id="libraryPanel" style="position:relative">
        <div class="lib-spinner-overlay" id="libSpinner"><div class="lib-spinner"></div><div class="lib-spinner-text" id="libSpinnerText">Loading...</div></div>
        <div class="lib-tabs">
            <button class="lib-tab active" id="libTabLocal" onclick="switchLibTab('local')">My Library</button>
            <button class="lib-tab" id="libTabCommunity" onclick="switchLibTab('community')">Community</button>
        </div>

        <!-- Local library content -->
        <div id="libLocalContent">
            <div class="lib-actions">
                <button class="lib-action-btn" onclick="saveToLibrary('item')" title="Save selected widget">+ Item</button>
                <button class="lib-action-btn" onclick="saveToLibrary('zone')" title="Save entire zone">+ Zone</button>
                <button class="lib-action-btn" onclick="saveToLibrary('layout')" title="Save entire layout">+ Layout</button>
                <button class="lib-action-btn accent" id="libApplyBtn" onclick="applyFromLibrary()" disabled title="Apply selected entry">Apply</button>
                <button class="lib-action-btn" id="libUploadBtn" onclick="uploadToComm()" disabled title="Share to community">Share</button>
                <button class="lib-action-btn" id="libDeleteBtn" onclick="deleteFromLibrary()" disabled style="color:#ff4444;border-color:#ff4444" title="Delete selected entry">Delete</button>
            </div>
            <div class="lib-entries" id="libLocalEntries">
                <div class="lib-empty">No saved entries yet.<br>Select a widget and click "+ Item" to save it.</div>
            </div>
        </div>

        <!-- Community content -->
        <div id="libCommunityContent" style="display:none">
            <div class="lib-actions">
                <button class="lib-action-btn accent" id="commApplyBtn" onclick="applyFromLibrary()" disabled title="Download &amp; apply">Apply</button>
                <button class="lib-action-btn" id="commDownloadBtn" onclick="downloadOnly()" disabled title="Download to My Library">Download</button>
            </div>
            <div class="lib-community-filters">
                <select class="lib-filter-select" id="commTypeFilter" onchange="browseCommunity()">
                    <option value="">All types</option>
                    <option value="item">Items</option>
                    <option value="zone">Zones</option>
                    <option value="layout">Layouts</option>
                </select>
                <input class="lib-filter-input" id="commSearchInput" type="text" placeholder="Search..."
                       onkeydown="if(event.key==='Enter')browseCommunity()">
                <select class="lib-filter-select" id="commSortFilter" onchange="browseCommunity()">
                    <option value="newest">Newest</option>
                    <option value="popular">Popular</option>
                    <option value="name">Name</option>
                </select>
            </div>
            <div class="lib-entries" id="libCommunityEntries">
                <div class="lib-empty">Click a tab or search to browse community entries.</div>
            </div>
            <div class="lib-load-more" id="commLoadMore" style="display:none" onclick="loadMoreCommunity()">Load more...</div>
        </div>
    </div>
    </div><!-- end main-wrapper -->

    <!-- Toast notification -->
    <div class="lib-toast" id="libToast"></div>
)HTML";
    html += R"HTML(
    <!-- Color picker popup with theme swatches -->
    <div class="color-popup-overlay" id="colorPopupOverlay" onclick="if(event.target===this)closeColorPopup()">
        <div class="color-popup" id="colorPopupBox">
            <h4 id="colorPopupTitle">Text Color</h4>

            <div class="color-popup-group">
                <div class="color-popup-group-label">Text</div>
                <div class="color-popup-grid">
                    <div class="cp-swatch" style="background:#ffffff" title="White" onclick="pickColor('#ffffff')"></div>
                    <div class="cp-swatch" style="background:#e6edf3" title="Light" onclick="pickColor('#e6edf3')"></div>
                    <div class="cp-swatch" style="background:#c0c5e3" title="Secondary" onclick="pickColor('#c0c5e3')"></div>
                    <div class="cp-swatch" style="background:#8b949e" title="Muted" onclick="pickColor('#8b949e')"></div>
                    <div class="cp-swatch" style="background:#000000;border-color:var(--border)" title="Black" onclick="pickColor('#000000')"></div>
                </div>
            </div>

            <div class="color-popup-group">
                <div class="color-popup-group-label">UI</div>
                <div class="color-popup-grid">
                    <div class="cp-swatch" style="background:#4e85f4" title="Primary" onclick="pickColor('#4e85f4')"></div>
                    <div class="cp-swatch" style="background:#e94560" title="Accent" onclick="pickColor('#e94560')"></div>
                    <div class="cp-swatch" style="background:#c9a227" title="Gold" onclick="pickColor('#c9a227')"></div>
                    <div class="cp-swatch" style="background:#00cc6d" title="Success" onclick="pickColor('#00cc6d')"></div>
                    <div class="cp-swatch" style="background:#ffaa00" title="Warning" onclick="pickColor('#ffaa00')"></div>
                    <div class="cp-swatch" style="background:#ff4444" title="Error" onclick="pickColor('#ff4444')"></div>
                </div>
            </div>

            <div class="color-popup-group">
                <div class="color-popup-group-label">Graph</div>
                <div class="color-popup-grid">
                    <div class="cp-swatch" style="background:#18c37e" title="Pressure" onclick="pickColor('#18c37e')"></div>
                    <div class="cp-swatch" style="background:#69fdb3" title="Pressure Goal" onclick="pickColor('#69fdb3')"></div>
                    <div class="cp-swatch" style="background:#4e85f4" title="Flow" onclick="pickColor('#4e85f4')"></div>
                    <div class="cp-swatch" style="background:#7aaaff" title="Flow Goal" onclick="pickColor('#7aaaff')"></div>
                    <div class="cp-swatch" style="background:#e73249" title="Temperature" onclick="pickColor('#e73249')"></div>
                    <div class="cp-swatch" style="background:#ffa5a6" title="Temp Goal" onclick="pickColor('#ffa5a6')"></div>
                    <div class="cp-swatch" style="background:#a2693d" title="Weight" onclick="pickColor('#a2693d')"></div>
                </div>
            </div>

            <div class="color-popup-group">
                <div class="color-popup-group-label">Extras</div>
                <div class="color-popup-grid">
                    <div class="cp-swatch" style="background:#9C27B0" title="Purple" onclick="pickColor('#9C27B0')"></div>
                    <div class="cp-swatch" style="background:#FF9800" title="Orange" onclick="pickColor('#FF9800')"></div>
                    <div class="cp-swatch" style="background:#6F4E37" title="Coffee" onclick="pickColor('#6F4E37')"></div>
                    <div class="cp-swatch" style="background:#555555" title="Grey" onclick="pickColor('#555555')"></div>
                    <div class="cp-swatch" style="background:#252538" title="Surface" onclick="pickColor('#252538')"></div>
                    <div class="cp-swatch" style="background:#1a1a2e;border-color:var(--border)" title="Dark" onclick="pickColor('#1a1a2e')"></div>
                </div>
            </div>

            <div class="cp-picker-row">
                <div id="iroPickerContainer"></div>
                <div class="cp-picker-right">
                    <div class="cp-hex-row">
                        <label>Hex</label>
                        <input type="text" class="cp-hex-input" id="cpHexInput" value="#ffffff" maxlength="7"
                               oninput="onHexInput(this.value)">
                    </div>
                    <div class="cp-preview-swatch" id="cpPreviewSwatch" style="background:#ffffff"></div>
                </div>
            </div>

            <div class="color-popup-footer">
                <button class="cp-apply-btn" onclick="applyPickerColor()">Apply</button>
            </div>
        </div>
    </div>
)HTML";

    // Part 5: JavaScript
    html += R"HTML(
    <script>
)HTML";
    html += WEB_JS_MENU;
    html += R"HTML(

    var layoutData = null;
    var selectedChip = null; // {id, zone}
    var editingItem = null;  // {id, zone}
    var currentAlign = "center";
    var currentAction = "";
    var currentLongPressAction = "";
    var currentDoubleclickAction = "";
    var currentEmoji = "";
    var currentBgColor = "";
    var emojiCategory = 0;
    var itemPropsCache = {}; // id -> {emoji, content, backgroundColor, ...}

    var DECENZA_ICONS = [
        {value:"/icons/espresso.svg",label:"Espresso"},
        {value:"/icons/steam.svg",label:"Steam"},
        {value:"/icons/water.svg",label:"Water"},
        {value:"/icons/flush.svg",label:"Flush"},
        {value:"/icons/coffeebeans.svg",label:"Beans"},
        {value:"/icons/sleep.svg",label:"Sleep"},
        {value:"/icons/settings.svg",label:"Settings"},
        {value:"/icons/history.svg",label:"History"},
        {value:"/icons/star.svg",label:"Star"},
        {value:"/icons/star-outline.svg",label:"Star"},
        {value:"/icons/temperature.svg",label:"Temp"},
        {value:"/icons/tea.svg",label:"Tea"},
        {value:"/icons/grind.svg",label:"Grind"},
        {value:"/icons/filter.svg",label:"Filter"},
        {value:"/icons/bluetooth.svg",label:"BT"},
        {value:"/icons/wifi.svg",label:"WiFi"},
        {value:"/icons/edit.svg",label:"Edit"},
        {value:"/icons/sparkle.svg",label:"AI"},
        {value:"/icons/hand.svg",label:"Hand"},
        {value:"/icons/tick.svg",label:"Tick"},
        {value:"/icons/cross.svg",label:"Cross"},
        {value:"/icons/decent-de1.svg",label:"DE1"},
        {value:"/icons/scale.svg",label:"Scale"},
        {value:"/icons/quit.svg",label:"Quit"},
        {value:"/icons/Graph.svg",label:"Graph"}
    ];

    var EMOJI_CATEGORIES = [
        {name:"Decenza",isSvg:true},
        {name:"Symbols",emoji:["✅","❌","❗","❓","⚠️","🚫","⭐","✨","💡","🔋","🔌","🔔","🔒","🔓","🔑","🔄","🔴","🟠","🟡","🟢","🔵","🟣","⚫","⚪","❤️","🧡","💛","💚","💙","💜","🖤","⬆️","⬇️","➡️","⬅️","➕","➖","▶️","⏸️","⏹️","🔅","🔆"]},
        {name:"Food",emoji:["☕","🫖","🍵","🧋","🥤","🍺","🍷","🍸","🥃","🥛","🧊","🍇","🍊","🍎","🍒","🥑","🍞","🍔","🍕","🍳","🍱","🍜","🍦","🍩","🎂","🍫","🍴","🥄"]},
        {name:"Objects",emoji:["🔧","⚙️","🛠️","🔨","⚖️","🔗","🧪","💻","📱","📷","💡","⌚","⏰","⏱️","📝","📈","📉","📊","🎵","🎧","🏆","🎯","💎"]},
        {name:"Nature",emoji:["☀️","⛅","☁️","❄️","🌈","🌡️","⚡","💧","🔥","🌙","🌍","🌱","💐","🌹","🍁","🐶","🐱"]},
        {name:"Smileys",emoji:["😀","😄","😊","😍","😎","🤔","😴","🤯","😱","👍","👎","👏","🙏","💪","👀","💯","💥"]},
        {name:"Activity",emoji:["⚽","🏀","🎾","🏆","🥇","🎉","🎊","🎁","🎯","🎲","🎨","🎮"]}
    ];

    var ZONES = [
        {key: "statusBar", label: "Status Bar (All Pages)", hasOffset: false},
        {key: "topLeft", label: "Top Bar (Left)", hasOffset: false},
        {key: "topRight", label: "Top Bar (Right)", hasOffset: false},
        {key: "centerStatus", label: "Center - Top", hasOffset: true},
        {key: "centerTop", label: "Center - Action Buttons", hasOffset: true},
        {key: "centerMiddle", label: "Center - Info", hasOffset: true},
        {key: "bottomLeft", label: "Bottom Bar (Left)", hasOffset: false},
        {key: "bottomRight", label: "Bottom Bar (Right)", hasOffset: false}
    ];

    var WIDGET_TYPES = [
        {type:"espresso",label:"Espresso"},{type:"steam",label:"Steam"},
        {type:"hotwater",label:"Hot Water"},{type:"flush",label:"Flush"},
        {type:"beans",label:"Beans"},{type:"history",label:"History"},
        {type:"autofavorites",label:"Favorites"},{type:"sleep",label:"Sleep"},
        {type:"settings",label:"Settings"},{type:"temperature",label:"Temperature"},
        {type:"steamTemperature",label:"Steam Temp"},
        {type:"waterLevel",label:"Water Level"},{type:"connectionStatus",label:"Connection"},
        {type:"scaleWeight",label:"Scale Weight"},{type:"shotPlan",label:"Shot Plan"},
        {type:"pageTitle",label:"Page Title",special:true},
        {type:"spacer",label:"Spacer",special:true},{type:"separator",label:"Separator",special:true},
        {type:"custom",label:"Custom",special:true},
        {type:"weather",label:"Weather",special:true},
        {type:"quit",label:"Quit",special:true}
    ];

    var DISPLAY_NAMES = {
        espresso:"Espresso",steam:"Steam",hotwater:"Hot Water",flush:"Flush",
        beans:"Beans",history:"History",autofavorites:"Favorites",sleep:"Sleep",
        settings:"Settings",temperature:"Temp",steamTemperature:"Steam",
        waterLevel:"Water",connectionStatus:"Connection",scaleWeight:"Scale",
        shotPlan:"Shot Plan",pageTitle:"Title",spacer:"Spacer",separator:"Sep",
        custom:"Custom",weather:"Weather",quit:"Quit"
    };

    var ACTIONS = [
        {id:"",label:"None"},
        {id:"navigate:settings",label:"Go to Settings"},
        {id:"navigate:history",label:"Go to History"},
        {id:"navigate:profiles",label:"Go to Profiles"},
        {id:"navigate:profileEditor",label:"Go to Profile Editor"},
        {id:"navigate:recipes",label:"Go to Recipes"},
        {id:"navigate:descaling",label:"Go to Descaling"},
        {id:"navigate:ai",label:"Go to AI Settings"},
        {id:"navigate:visualizer",label:"Go to Visualizer"},
        {id:"navigate:autofavorites",label:"Go to Auto-Favorites"},
        {id:"command:sleep",label:"Sleep"},
        {id:"command:startEspresso",label:"Start Espresso"},
        {id:"command:startSteam",label:"Start Steam"},
        {id:"command:startHotWater",label:"Start Hot Water"},
        {id:"command:startFlush",label:"Start Flush"},
        {id:"command:idle",label:"Stop (Idle)"},
        {id:"command:tare",label:"Tare Scale"},
        {id:"command:quit",label:"Quit App"}
    ];
)HTML";
    html += R"HTML(
    function loadLayout() {
        fetch("/api/layout").then(function(r){return r.json()}).then(function(data) {
            layoutData = data;
            // Fetch properties for custom items to render mini previews
            var customIds = [];
            if (data && data.zones) {
                for (var zk in data.zones) {
                    var zoneItems = data.zones[zk] || [];
                    for (var ci = 0; ci < zoneItems.length; ci++) {
                        if (zoneItems[ci].type === "custom" && !itemPropsCache[zoneItems[ci].id]) {
                            customIds.push(zoneItems[ci].id);
                        }
                    }
                }
            }
            if (customIds.length > 0) {
                var loaded = 0;
                for (var pi = 0; pi < customIds.length; pi++) {
                    (function(cid) {
                        fetch("/api/layout/item?id=" + encodeURIComponent(cid))
                            .then(function(r){return r.json()})
                            .then(function(props) {
                                itemPropsCache[cid] = props;
                                loaded++;
                                if (loaded >= customIds.length) renderZones();
                            });
                    })(customIds[pi]);
                }
            } else {
                renderZones();
            }
        });
    }

    function stripHtml(text) {
        var tmp = document.createElement("div");
        tmp.innerHTML = text;
        return tmp.textContent || tmp.innerText || "";
    }
)HTML";
    html += R"HTML(
    function renderZones() {
        var panel = document.getElementById("zonesPanel");
        var html = "";
        for (var z = 0; z < ZONES.length; z++) {
            var zone = ZONES[z];
            var items = (layoutData && layoutData.zones && layoutData.zones[zone.key]) || [];

            // Pair top and bottom zones side by side
            var isPairStart = (zone.key === "topLeft" || zone.key === "bottomLeft");
            var isPairEnd = (zone.key === "topRight" || zone.key === "bottomRight");
            if (isPairStart) html += '<div class="zone-row">';

            var zoneSelected = selectedChip && selectedChip.zone === zone.key;
            html += '<div class="zone-card' + (zoneSelected ? ' selected' : '') + '" style="' + (isPairStart || isPairEnd ? 'flex:1' : '') + '" onclick="zoneClick(\'' + zone.key + '\',event)">';
            html += '<div class="zone-header"><span class="zone-title">' + zone.label + '</span>';

            if (zone.hasOffset) {
                var offset = 0;
                if (layoutData && layoutData.offsets && layoutData.offsets[zone.key] !== undefined)
                    offset = layoutData.offsets[zone.key];
                html += '<div class="zone-offset-controls">';
                html += '<button class="offset-btn" onclick="changeOffset(\'' + zone.key + '\',-5)">&#9650;</button>';
                html += '<span class="offset-val">' + (offset !== 0 ? (offset > 0 ? "+" : "") + offset : "0") + '</span>';
                html += '<button class="offset-btn" onclick="changeOffset(\'' + zone.key + '\',5)">&#9660;</button>';
                html += '</div>';
            }
            html += '</div>';

            html += '<div class="chips-area">';
            for (var i = 0; i < items.length; i++) {
                var item = items[i];
                var isSpecial = item.type === "spacer" || item.type === "custom" || item.type === "weather" || item.type === "separator" || item.type === "pageTitle" || item.type === "quit";
                var isSel = selectedChip && selectedChip.id === item.id;
                var cls = "chip" + (isSel ? " selected" : "") + (isSpecial ? " special" : "");
                var chipStyle = "";
                var props = item.type === "custom" ? itemPropsCache[item.id] : null;
                if (props && props.backgroundColor && !isSel) {
                    chipStyle = "background:" + props.backgroundColor + ";border-color:" + props.backgroundColor + ";color:white";
                }
                html += '<span class="' + cls + '" style="' + chipStyle + '" onclick="chipClick(\'' + item.id + '\',\'' + zone.key + '\',\'' + item.type + '\')">';

                if (isSel && i > 0) {
                    html += '<span class="chip-arrow" onclick="event.stopPropagation();reorder(\'' + zone.key + '\',' + i + ',' + (i-1) + ')">&#9664;</span>';
                }
                // Mini preview for custom items
                if (item.type === "custom" && props) {
                    if (props.emoji) {
                        if (props.emoji.indexOf("qrc:") === 0) {
                            html += '<span class="chip-emoji"><img src="' + props.emoji.replace("qrc:","") + '"></span>';
                        } else {
                            html += '<span class="chip-emoji">' + props.emoji + '</span>';
                        }
                    }
                    var chipLabel = stripHtml(props.content || "");
                    chipLabel = chipLabel.length > 12 ? chipLabel.substring(0, 10) + ".." : (chipLabel || "Custom");
                    html += chipLabel;
                } else {
                    html += DISPLAY_NAMES[item.type] || item.type;
                }
                if (isSel && i < items.length - 1) {
                    html += '<span class="chip-arrow" onclick="event.stopPropagation();reorder(\'' + zone.key + '\',' + i + ',' + (i+1) + ')">&#9654;</span>';
                }
                if (isSel) {
                    html += '<span class="chip-remove" onclick="event.stopPropagation();removeItem(\'' + item.id + '\',\'' + zone.key + '\')">&times;</span>';
                    if (item.type !== "custom" && item.type !== "spacer" && item.type !== "separator") {
                        html += '<span class="convert-btn" onclick="event.stopPropagation();convertToCustom(\'' + item.id + '\',\'' + zone.key + '\')">&#8594; Custom</span>';
                    }
                }
                html += '</span>';
            }

            // Add button with dropdown
            html += '<div style="position:relative;display:inline-block">';
            html += '<button class="add-btn" onclick="event.stopPropagation();toggleAddMenu(this)">+</button>';
            html += '<div class="add-dropdown">';
            for (var w = 0; w < WIDGET_TYPES.length; w++) {
                var wt = WIDGET_TYPES[w];
                html += '<div class="add-dropdown-item' + (wt.special ? ' special' : '') + '" ';
                html += 'onclick="event.stopPropagation();addItem(\'' + wt.type + '\',\'' + zone.key + '\');this.parentElement.classList.remove(\'open\')">';
                html += wt.label + '</div>';
            }
            html += '</div></div>';

            html += '</div></div>';

            if (isPairEnd) html += '</div>';
        }
        panel.innerHTML = html;
    }
)HTML";

    // Part 5b: Layout editor JS - interaction handlers
    html += R"HTML(
    function zoneClick(zoneKey, event) {
        // Only handle clicks on the zone card itself, not on chips/buttons inside
        if (event.target.closest('.chip, .add-btn, .add-dropdown, .offset-btn')) return;
        if (selectedChip && selectedChip.zone === zoneKey && !selectedChip.id) {
            selectedChip = null;
        } else {
            selectedChip = {id: null, zone: zoneKey};
        }
        renderZones();
    }

    function chipClick(itemId, zone, type) {
        if (selectedChip && selectedChip.id === itemId) {
            // Deselect
            selectedChip = null;
        } else {
            selectedChip = {id: itemId, zone: zone};
            if (type === "custom") {
                openEditor(itemId, zone);
            }
        }
        renderZones();
    }

    function convertToCustom(itemId, zone) {
        apiPost("/api/layout/item", {itemId: itemId, key: "type", value: "custom"}, function() {
            itemPropsCache[itemId] = null;
            selectedChip = {id: itemId, zone: zone};
            loadLayout();
            openEditor(itemId, zone);
        });
    }

    function toggleAddMenu(btn) {
        var dropdown = btn.nextElementSibling;
        // Close all other dropdowns
        document.querySelectorAll(".add-dropdown.open").forEach(function(d) {
            if (d !== dropdown) d.classList.remove("open");
        });
        dropdown.classList.toggle("open");
    }

    // Close dropdowns when clicking outside
    document.addEventListener("click", function(e) {
        if (!e.target.closest(".add-btn") && !e.target.closest(".add-dropdown")) {
            document.querySelectorAll(".add-dropdown.open").forEach(function(d) { d.classList.remove("open"); });
        }
    });

    function addItem(type, zone) {
        apiPost("/api/layout/add", {type: type, zone: zone}, function() {
            loadLayout();
        });
    }

    function removeItem(itemId, zone) {
        apiPost("/api/layout/remove", {itemId: itemId, zone: zone}, function() {
            if (selectedChip && selectedChip.id === itemId) selectedChip = null;
            if (editingItem && editingItem.id === itemId) closeEditor();
            loadLayout();
        });
    }

    function reorder(zone, fromIdx, toIdx) {
        apiPost("/api/layout/reorder", {zone: zone, fromIndex: fromIdx, toIndex: toIdx}, function() {
            loadLayout();
        });
    }

    function changeOffset(zone, delta) {
        var current = 0;
        if (layoutData && layoutData.offsets && layoutData.offsets[zone] !== undefined)
            current = layoutData.offsets[zone];
        apiPost("/api/layout/zone-offset", {zone: zone, offset: current + delta}, function() {
            loadLayout();
        });
    }

    function resetLayout() {
        if (!confirm("Reset layout to default?")) return;
        apiPost("/api/layout/reset", {}, function() {
            selectedChip = null;
            closeEditor();
            loadLayout();
        });
    }

    // ---- WYSIWYG Text Editor ----

    var wysiwygEl = document.getElementById("wysiwygEditor");
    var actionPickerGesture = "";
    var savedRange = null;

    // Save/restore selection so color pickers etc. don't lose it
    function saveSelection() {
        var sel = window.getSelection();
        if (sel.rangeCount > 0 && wysiwygEl.contains(sel.anchorNode)) {
            savedRange = sel.getRangeAt(0).cloneRange();
        }
    }
    function restoreSelection() {
        if (savedRange) {
            wysiwygEl.focus();
            var sel = window.getSelection();
            sel.removeAllRanges();
            sel.addRange(savedRange);
        } else {
            wysiwygEl.focus();
        }
    }
    wysiwygEl.addEventListener("mouseup", saveSelection);
    wysiwygEl.addEventListener("keyup", saveSelection);
    wysiwygEl.addEventListener("blur", saveSelection);
)HTML";
    html += R"HTML(
    // ---- Segment model (portable rich text format) ----

    function rgbToHex(rgb) {
        if (!rgb) return "";
        if (rgb.charAt(0) === "#") return rgb;
        var m = rgb.match(/(\d+)/g);
        if (!m || m.length < 3) return rgb;
        return "#" + ((1<<24)+(parseInt(m[0])<<16)+(parseInt(m[1])<<8)+parseInt(m[2])).toString(16).slice(1);
    }

    function domToSegments(el) {
        var segments = [];
        function walk(node, inherited) {
            if (node.nodeType === 3) {
                var text = node.textContent;
                if (text) {
                    var seg = {text: text};
                    if (inherited.bold) seg.bold = true;
                    if (inherited.italic) seg.italic = true;
                    if (inherited.color) seg.color = inherited.color;
                    if (inherited.size) seg.size = inherited.size;
                    segments.push(seg);
                }
                return;
            }
            if (node.nodeType !== 1) return;
            var tag = node.tagName.toLowerCase();
            var fmt = {};
            for (var k in inherited) fmt[k] = inherited[k];
            if (tag === "b" || tag === "strong") fmt.bold = true;
            if (tag === "i" || tag === "em") fmt.italic = true;
            if (tag === "br") { segments.push({text: "\n"}); return; }
            // Browsers create <div> or <p> on Enter — treat as line break before content
            if ((tag === "div" || tag === "p") && segments.length > 0) {
                var lastSeg = segments[segments.length - 1];
                if (lastSeg.text !== "\n") segments.push({text: "\n"});
            }
            var st = node.style;
            if (st.color) fmt.color = rgbToHex(st.color);
            if (st.fontSize) { var px = parseInt(st.fontSize); if (px > 0) fmt.size = px; }
            if (st.fontWeight === "bold" || parseInt(st.fontWeight) >= 700) fmt.bold = true;
            if (st.fontStyle === "italic") fmt.italic = true;
            if (tag === "font" && node.getAttribute("color")) fmt.color = rgbToHex(node.getAttribute("color"));
            for (var i = 0; i < node.childNodes.length; i++) walk(node.childNodes[i], fmt);
        }
        walk(el, {});
        return mergeAdjacentSegments(segments);
    }

    function mergeAdjacentSegments(segments) {
        if (segments.length <= 1) return segments;
        var result = [];
        for (var i = 0; i < segments.length; i++) {
            var cur = segments[i];
            if (result.length === 0) { result.push({text:cur.text, bold:cur.bold, italic:cur.italic, color:cur.color, size:cur.size}); continue; }
            var prev = result[result.length - 1];
            if (prev.text !== "\n" && cur.text !== "\n" &&
                !!prev.bold === !!cur.bold && !!prev.italic === !!cur.italic &&
                (prev.color||"") === (cur.color||"") && (prev.size||0) === (cur.size||0)) {
                prev.text += cur.text;
            } else {
                result.push({text:cur.text, bold:cur.bold, italic:cur.italic, color:cur.color, size:cur.size});
            }
        }
        // Clean undefined values
        for (var j = 0; j < result.length; j++) {
            var s = result[j];
            if (!s.bold) delete s.bold;
            if (!s.italic) delete s.italic;
            if (!s.color) delete s.color;
            if (!s.size) delete s.size;
        }
        return result;
    }

    function segmentsToHtml(segments) {
        var html = "";
        for (var i = 0; i < segments.length; i++) {
            var seg = segments[i];
            var text = seg.text;
            if (!text) continue;
            if (text === "\n") { html += "<br>"; continue; }
            var escaped = text.replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;");
            var styles = [];
            if (seg.color) styles.push("color:" + seg.color);
            if (seg.size) styles.push("font-size:" + seg.size + "px");
            if (styles.length > 0) escaped = '<span style="' + styles.join("; ") + '">' + escaped + '</span>';
            if (seg.bold) escaped = "<b>" + escaped + "</b>";
            if (seg.italic) escaped = "<i>" + escaped + "</i>";
            html += escaped;
        }
        return html || "Text";
    }
)HTML";
    html += R"HTML(
    function openEditor(itemId, zone) {
        // Flush any pending auto-save from previously edited item
        if (editingItem && autoSaveTimer) {
            clearTimeout(autoSaveTimer); autoSaveTimer = null; saveText();
        }
        editingItem = {id: itemId, zone: zone};
        document.getElementById("emojiPickerArea").style.display = "none";
        document.getElementById("emojiToggleBtn").textContent = "Pick Icon";
        fetch("/api/layout/item?id=" + encodeURIComponent(itemId))
            .then(function(r){return r.json()})
            .then(function(props) {
                // Load segments if available, otherwise fall back to HTML content
                if (props.segments && props.segments.length > 0) {
                    wysiwygEl.innerHTML = segmentsToHtml(props.segments);
                } else {
                    wysiwygEl.innerHTML = props.content || "Text";
                }
                currentAlign = props.align || "center";
                currentAction = props.action || "";
                currentLongPressAction = props.longPressAction || "";
                currentDoubleclickAction = props.doubleclickAction || "";
                currentEmoji = props.emoji || "";
                currentBgColor = props.backgroundColor || "";
                wysiwygEl.style.textAlign = currentAlign;
                updateAlignButtons();
                updateActionSelectors();
                updateIconPreview();
                renderEmojiTabs();
                renderEmojiGrid();
                updateBgColorUI();
                updateTextColorUI("#ffffff");
                updatePreview();
                document.getElementById("editorPanel").classList.remove("editor-hidden");
                wysiwygEl.focus();
            });
    }

    function closeEditor() {
        // Flush any pending auto-save before closing
        if (autoSaveTimer) { clearTimeout(autoSaveTimer); autoSaveTimer = null; saveText(); }
        editingItem = null;
        document.getElementById("editorPanel").classList.add("editor-hidden");
    }

    var autoSaveTimer = null;
    function autoSave() {
        if (autoSaveTimer) clearTimeout(autoSaveTimer);
        autoSaveTimer = setTimeout(function() { autoSaveTimer = null; saveText(); }, 500);
    }

    function saveText() {
        if (!editingItem) return;
        // Extract segments from the contenteditable DOM and compile to HTML
        var segments = domToSegments(wysiwygEl);
        var content = segmentsToHtml(segments);
        if (!content || content === "<br>") content = "Text";
        var id = editingItem.id;
        var done = 0;
        var total = 8;
        function check() { done++; if (done >= total) { itemPropsCache[id] = null; loadLayout(); } }
        apiPost("/api/layout/item", {itemId: id, key: "content", value: content}, check);
        apiPost("/api/layout/item", {itemId: id, key: "segments", value: segments}, check);
        apiPost("/api/layout/item", {itemId: id, key: "align", value: currentAlign}, check);
        apiPost("/api/layout/item", {itemId: id, key: "action", value: currentAction}, check);
        apiPost("/api/layout/item", {itemId: id, key: "longPressAction", value: currentLongPressAction}, check);
        apiPost("/api/layout/item", {itemId: id, key: "doubleclickAction", value: currentDoubleclickAction}, check);
        apiPost("/api/layout/item", {itemId: id, key: "emoji", value: currentEmoji}, check);
        apiPost("/api/layout/item", {itemId: id, key: "backgroundColor", value: currentBgColor}, check);
    }
)HTML";
    html += R"HTML(
    // ---- WYSIWYG formatting (execCommand) ----

    function execBold() {
        restoreSelection();
        document.execCommand("bold", false, null);
        saveSelection();
        updatePreview();
        autoSave();
    }

    function execItalic() {
        restoreSelection();
        document.execCommand("italic", false, null);
        saveSelection();
        updatePreview();
        autoSave();
    }

    function execFontSize(px) {
        restoreSelection();
        var sel = window.getSelection();
        if (!sel.rangeCount || sel.isCollapsed) return;
        // Use execCommand fontSize (handles cross-boundary selections correctly)
        // then convert the <font size="7"> tags to proper <span style="font-size:...">
        document.execCommand("fontSize", false, "7");
        var fonts = wysiwygEl.querySelectorAll('font[size="7"]');
        for (var i = 0; i < fonts.length; i++) {
            var span = document.createElement("span");
            span.style.fontSize = px + "px";
            while (fonts[i].firstChild) span.appendChild(fonts[i].firstChild);
            fonts[i].parentNode.replaceChild(span, fonts[i]);
        }
        saveSelection();
        updatePreview();
        autoSave();
    }

    function applyTextColor(color) {
        restoreSelection();
        document.execCommand("foreColor", false, color);
        saveSelection();
        updateTextColorUI(color);
        updatePreview();
        autoSave();
    }

    function clearTextColor() {
        restoreSelection();
        // Apply default color instead of removeFormat (which strips ALL formatting)
        document.execCommand("foreColor", false, "#ffffff");
        saveSelection();
        updateTextColorUI("#ffffff");
        updatePreview();
        autoSave();
    }
)HTML";
    html += R"HTML(
    // --- Color popup with iro.js picker + theme swatches ---
    var colorPopupMode = "text"; // "text" or "bg"
    var iroPicker = null;
    var iroPickerColor = "#ffffff";

    function initIroPicker() {
        if (iroPicker) return;
        iroPicker = new iro.ColorPicker("#iroPickerContainer", {
            width: 140,
            color: "#ffffff",
            borderWidth: 1,
            borderColor: "#444",
            layoutDirection: "vertical",
            layout: [
                { component: iro.ui.Wheel },
                { component: iro.ui.Slider, options: { sliderType: "value" } }
            ]
        });
        iroPicker.on("color:change", function(color) {
            iroPickerColor = color.hexString;
            document.getElementById("cpHexInput").value = iroPickerColor;
            document.getElementById("cpPreviewSwatch").style.background = iroPickerColor;
        });
    }

    function openColorPopup(mode) {
        saveSelection();
        colorPopupMode = mode;
        document.getElementById("colorPopupTitle").textContent = mode === "text" ? "Text Color" : "Background Color";
        // Reset dragged position so it centers
        var box = document.getElementById("colorPopupBox");
        box.style.position = "";
        box.style.left = "";
        box.style.top = "";
        // Initialize iro.js picker (lazy, first open)
        initIroPicker();
        // Set picker to current color
        var startColor = mode === "bg" ? (currentBgColor || "#555555") : "#ffffff";
        iroPicker.color.hexString = startColor;
        iroPickerColor = startColor;
        document.getElementById("cpHexInput").value = startColor;
        document.getElementById("cpPreviewSwatch").style.background = startColor;
        document.getElementById("colorPopupOverlay").classList.add("open");
    }

    function closeColorPopup() {
        document.getElementById("colorPopupOverlay").classList.remove("open");
    }

    function pickColor(color) {
        if (colorPopupMode === "text") {
            applyTextColor(color);
        } else {
            setBgColor(color);
        }
        closeColorPopup();
    }

    function applyPickerColor() {
        pickColor(iroPickerColor);
    }

    function onHexInput(val) {
        if (/^#[0-9a-fA-F]{6}$/.test(val)) {
            iroPickerColor = val;
            iroPicker.color.hexString = val;
            document.getElementById("cpPreviewSwatch").style.background = val;
        }
    }

    // Make popup draggable
    (function() {
        var box = document.getElementById("colorPopupBox");
        var dragging = false, startX, startY, origX, origY;
        box.querySelector("h4").style.cursor = "move";
        box.querySelector("h4").addEventListener("mousedown", function(e) {
            dragging = true;
            startX = e.clientX; startY = e.clientY;
            var rect = box.getBoundingClientRect();
            origX = rect.left; origY = rect.top;
            box.style.position = "fixed";
            box.style.margin = "0";
            e.preventDefault();
        });
        document.addEventListener("mousemove", function(e) {
            if (!dragging) return;
            box.style.left = (origX + e.clientX - startX) + "px";
            box.style.top = (origY + e.clientY - startY) + "px";
        });
        document.addEventListener("mouseup", function() { dragging = false; });
    })();

    function updateTextColorUI(color) {
        document.getElementById("textColorSwatch").style.background = color;
        document.getElementById("textColorInput").value = color;
    }

    function insertVar(token) {
        restoreSelection();
        document.execCommand("insertText", false, token);
        saveSelection();
        updatePreview();
        autoSave();
    }

    function setAlign(a) {
        currentAlign = a;
        wysiwygEl.style.textAlign = a;
        updateAlignButtons();
        updatePreview();
        autoSave();
    }

    function updateAlignButtons() {
        ["Left","Center","Right"].forEach(function(d) {
            var btn = document.getElementById("align" + d);
            if (btn) btn.classList.toggle("active", currentAlign === d.toLowerCase());
        });
    }
)HTML";
    html += R"HTML(
    // ---- Background Color ----

    function setBgColor(color) {
        currentBgColor = color;
        updateBgColorUI();
        updatePreview();
        autoSave();
    }

    function clearBgColor() {
        currentBgColor = "";
        updateBgColorUI();
        updatePreview();
        autoSave();
    }

    function updateBgColorUI() {
        var swatch = document.getElementById("bgColorSwatch");
        var noneX = document.getElementById("bgNoneX");
        var clearBtn = document.getElementById("bgClearBtn");
        if (currentBgColor) {
            swatch.style.background = currentBgColor;
            noneX.style.display = "none";
            clearBtn.style.display = "";
        } else {
            swatch.style.background = "transparent";
            noneX.style.display = "";
            clearBtn.style.display = "none";
        }
    }

)HTML";

    // Part 5b2: Layout editor JS - action picker, emoji, preview
    html += R"HTML(
    // ---- Action Picker (popup, matching tablet design) ----

    function getActionLabel(id) {
        if (!id) return "None";
        for (var i = 0; i < ACTIONS.length; i++) {
            if (ACTIONS[i].id === id) return ACTIONS[i].label;
        }
        return id;
    }

    function updateActionSelectors() {
        document.getElementById("tapActionLabel").textContent = getActionLabel(currentAction);
        document.getElementById("longPressActionLabel").textContent = getActionLabel(currentLongPressAction);
        document.getElementById("dblClickActionLabel").textContent = getActionLabel(currentDoubleclickAction);
        document.getElementById("tapActionSel").className = "action-selector" + (currentAction ? " has-action" : "");
        document.getElementById("longPressActionSel").className = "action-selector" + (currentLongPressAction ? " has-action" : "");
        document.getElementById("dblClickActionSel").className = "action-selector" + (currentDoubleclickAction ? " has-action" : "");
    }

    function openActionPicker(gesture) {
        actionPickerGesture = gesture;
        var titles = {click: "Tap Action", longpress: "Long Press Action", doubleclick: "Double-Click Action"};
        document.getElementById("actionPickerTitle").textContent = titles[gesture] || "Action";
        var currentVal = gesture === "click" ? currentAction : gesture === "longpress" ? currentLongPressAction : currentDoubleclickAction;
        var html = "";
        for (var i = 0; i < ACTIONS.length; i++) {
            var a = ACTIONS[i];
            var cls = "action-dialog-item" + (currentVal === a.id ? " selected" : "");
            html += '<div class="' + cls + '" onclick="pickAction(\'' + a.id + '\')">' + a.label + '</div>';
        }
        document.getElementById("actionPickerList").innerHTML = html;
        document.getElementById("actionOverlay").classList.add("open");
    }

    function closeActionPicker() {
        document.getElementById("actionOverlay").classList.remove("open");
    }

    function pickAction(id) {
        if (actionPickerGesture === "click") currentAction = id;
        else if (actionPickerGesture === "longpress") currentLongPressAction = id;
        else if (actionPickerGesture === "doubleclick") currentDoubleclickAction = id;
        updateActionSelectors();
        closeActionPicker();
        updatePreview();
        autoSave();
    }
)HTML";
    html += R"HTML(
    // ---- Emoji / Icon Picker ----

    function toggleEmojiPicker() {
        var area = document.getElementById("emojiPickerArea");
        var btn = document.getElementById("emojiToggleBtn");
        if (area.style.display === "none") {
            area.style.display = "";
            btn.textContent = "Hide Picker";
        } else {
            area.style.display = "none";
            btn.textContent = "Pick Icon";
        }
    }

    function renderEmojiTabs() {
        var html = "";
        for (var i = 0; i < EMOJI_CATEGORIES.length; i++) {
            var cls = "emoji-tab" + (emojiCategory === i ? " active" : "");
            html += '<span class="' + cls + '" onclick="setEmojiCategory(' + i + ')">' + EMOJI_CATEGORIES[i].name + '</span>';
        }
        document.getElementById("emojiTabs").innerHTML = html;
    }

    function renderEmojiGrid() {
        var cat = EMOJI_CATEGORIES[emojiCategory];
        var html = "";
        if (cat.isSvg) {
            for (var i = 0; i < DECENZA_ICONS.length; i++) {
                var icon = DECENZA_ICONS[i];
                var sel = currentEmoji === ("qrc:" + icon.value) ? " selected" : "";
                html += '<span class="emoji-cell' + sel + '" title="' + icon.label + '" onclick="selectEmoji(\'qrc:' + icon.value + '\')">';
                html += '<img src="' + icon.value + '" alt="' + icon.label + '">';
                html += '</span>';
            }
        } else {
            var emojis = cat.emoji || [];
            for (var i = 0; i < emojis.length; i++) {
                var e = emojis[i];
                var sel = currentEmoji === e ? " selected" : "";
                html += '<span class="emoji-cell' + sel + '" onclick="selectEmoji(\'' + e + '\')">' + e + '</span>';
            }
        }
        document.getElementById("emojiGrid").innerHTML = html;
    }

    function setEmojiCategory(idx) {
        emojiCategory = idx;
        renderEmojiTabs();
        renderEmojiGrid();
    }

    function selectEmoji(value) {
        currentEmoji = value;
        updateIconPreview();
        renderEmojiGrid();
        updatePreview();
        autoSave();
    }

    function clearEmoji() {
        currentEmoji = "";
        updateIconPreview();
        renderEmojiGrid();
        updatePreview();
        autoSave();
    }

    function updateIconPreview() {
        var preview = document.getElementById("iconPreview");
        var clearBtn = document.getElementById("emojiClearBtn");
        if (!currentEmoji) {
            preview.innerHTML = '<span style="color:var(--text-secondary)">&#8212;</span>';
            clearBtn.style.display = "none";
        } else if (currentEmoji.indexOf("qrc:") === 0) {
            var src = currentEmoji.replace("qrc:", "");
            preview.innerHTML = '<img src="' + src + '">';
            clearBtn.style.display = "";
        } else {
            preview.innerHTML = '<span style="font-size:1.5rem">' + currentEmoji + '</span>';
            clearBtn.style.display = "";
        }
    }
)HTML";
    html += R"HTML(
    // ---- Dual Preview (Full + Bar, matching tablet) ----

    function updatePreview() {
        var rawHtml = wysiwygEl.innerHTML || "";
        // Substitute variables in the formatted HTML (tokens are in text, not in tags)
        var formattedPreview = substitutePreview(rawHtml);
        var hasAction = currentAction || currentLongPressAction || currentDoubleclickAction;
        var hasEmoji = currentEmoji !== "";
        var bgColor = currentBgColor || ((hasAction || hasEmoji) ? "#555555" : "");
        var defaultColor = (hasAction || hasEmoji || currentBgColor) ? "white" : "var(--text)";

        // Full preview (center zones: vertical emoji + text)
        var fullEl = document.getElementById("previewFull");
        var fullHtml = "";
        if (hasEmoji) {
            if (currentEmoji.indexOf("qrc:") === 0) {
                fullHtml += '<img src="' + currentEmoji.replace("qrc:","") + '">';
            } else {
                fullHtml += '<span class="pv-emoji">' + currentEmoji + '</span>';
            }
        }
        fullHtml += '<span class="pv-text" style="color:' + defaultColor + '">' + formattedPreview + '</span>';
        fullEl.innerHTML = fullHtml;
        fullEl.style.background = bgColor || "var(--bg)";
        fullEl.style.textAlign = currentAlign;
        fullEl.className = "preview-full" + (hasAction ? " has-action" : "");

        // Bar preview (bar zones: horizontal emoji + text)
        var barEl = document.getElementById("previewBar");
        var barHtml = "";
        if (hasEmoji) {
            if (currentEmoji.indexOf("qrc:") === 0) {
                barHtml += '<img src="' + currentEmoji.replace("qrc:","") + '">';
            } else {
                barHtml += '<span class="pv-emoji">' + currentEmoji + '</span>';
            }
        }
        barHtml += '<span class="pv-text" style="color:' + defaultColor + '">' + formattedPreview + '</span>';
        barEl.innerHTML = barHtml;
        barEl.style.background = bgColor || "var(--bg)";
        barEl.className = "preview-bar" + (hasAction ? " has-action" : "");
    }

    function substitutePreview(t) {
        var now = new Date();
        var hh = String(now.getHours()).padStart(2,"0");
        var mm = String(now.getMinutes()).padStart(2,"0");
        return t
            .replace(/%TEMP%/g,"92.3").replace(/%STEAM_TEMP%/g,"155.0")
            .replace(/%PRESSURE%/g,"9.0").replace(/%FLOW%/g,"2.1")
            .replace(/%WATER%/g,"78").replace(/%WATER_ML%/g,"850")
            .replace(/%STATE%/g,"Idle").replace(/%WEIGHT%/g,"36.2")
            .replace(/%SHOT_TIME%/g,"28.5").replace(/%VOLUME%/g,"42")
            .replace(/%TARGET_WEIGHT%/g,"36.0").replace(/%PROFILE%/g,"Adaptive v2")
            .replace(/%TARGET_TEMP%/g,"93.0").replace(/%RATIO%/g,"2.0")
            .replace(/%DOSE%/g,"18.0").replace(/%SCALE%/g,"Lunar")
            .replace(/%CONNECTED%/g,"Online").replace(/%CONNECTED_COLOR%/g,"#18c37e")
            .replace(/%DEVICES%/g,"Machine + Scale")
            .replace(/%TIME%/g,hh+":"+mm)
            .replace(/%DATE%/g,now.toISOString().split("T")[0]);
    }

    // Live preview updates on WYSIWYG input
    wysiwygEl.addEventListener("input", function() { updatePreview(); autoSave(); });

    function apiPost(url, data, cb) {
        fetch(url, {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify(data)
        }).then(function(r){return r.json()}).then(function(result) {
            if (cb) cb(result);
        });
    }
)HTML";

    // Part 5c: Layout editor JS - AI dialog
    html += R"HTML(
    // ---- AI Dialog ----

    function openAiDialog() {
        document.getElementById("aiOverlay").classList.add("open");
        document.getElementById("aiPrompt").focus();
        document.getElementById("aiResultArea").innerHTML = "";
    }

    function closeAiDialog() {
        document.getElementById("aiOverlay").classList.remove("open");
    }

    function sendAiPrompt() {
        var prompt = document.getElementById("aiPrompt").value.trim();
        if (!prompt) return;
        var btn = document.getElementById("aiSendBtn");
        btn.disabled = true;
        btn.textContent = "Thinking...";
        document.getElementById("aiResultArea").innerHTML = '<div class="ai-result ai-loading">AI is generating your layout...</div>';

        fetch("/api/layout/ai", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify({prompt: prompt})
        })
        .then(function(r) { return r.json(); })
        .then(function(data) {
            btn.disabled = false;
            btn.textContent = "Generate";
            if (data.error) {
                document.getElementById("aiResultArea").innerHTML = '<div class="ai-result error">' + escapeHtml(data.error) + '</div>';
            } else if (data.success) {
                document.getElementById("aiResultArea").innerHTML = '<div class="ai-result success">Layout applied successfully!</div>';
                loadLayout();
            } else if (data.message) {
                document.getElementById("aiResultArea").innerHTML = '<div class="ai-result">' + escapeHtml(data.message) + '</div>';
            }
        })
        .catch(function(err) {
            btn.disabled = false;
            btn.textContent = "Generate";
            document.getElementById("aiResultArea").innerHTML = '<div class="ai-result error">Request failed: ' + escapeHtml(err.message) + '</div>';
        });
    }

    function escapeHtml(str) {
        if (!str) return '';
        var div = document.createElement("div");
        div.textContent = str;
        return div.innerHTML;
    }

    // ===== Library panel =====
    var libCurrentTab = 'local';
    var libLocalData = [];
    var libCommunityData = [];
    var libSelectedId = null;
    var commPage = 1;
    var commTotal = 0;

    function switchLibTab(tab) {
        libCurrentTab = tab;
        document.getElementById('libTabLocal').classList.toggle('active', tab === 'local');
        document.getElementById('libTabCommunity').classList.toggle('active', tab === 'community');
        document.getElementById('libLocalContent').style.display = tab === 'local' ? '' : 'none';
        document.getElementById('libCommunityContent').style.display = tab === 'community' ? '' : 'none';
        libSelectedId = null;
        updateLibButtons();
        if (tab === 'local') loadLibrary();
        else if (libCommunityData.length === 0) browseCommunity();
    }

    function loadLibrary() {
        fetch('/api/library/entries').then(function(r){return r.json()}).then(function(entries) {
            libLocalData = entries;
            renderLocalEntries();
        });
    }

    var SAMPLE_VARS = {
        "%TEMP%":"93.2","%STEAM_TEMP%":"155.0","%PRESSURE%":"9.0","%FLOW%":"2.1",
        "%WATER%":"78","%WATER_ML%":"850","%WEIGHT%":"36.2","%SHOT_TIME%":"28.5",
        "%TARGET_WEIGHT%":"36.0","%VOLUME%":"42","%PROFILE%":"Profile","%STATE%":"Idle",
        "%TARGET_TEMP%":"93.0","%SCALE%":"Scale","%RATIO%":"2.0","%DOSE%":"18.0",
        "%TIME%":"12:30","%DATE%":"2025-01-15","%CONNECTED%":"Online",
        "%CONNECTED_COLOR%":"#00cc6d","%DEVICES%":"Machine"
    };

    function resolveVars(text) {
        if (!text) return '';
        var r = text.replace(/<[^>]*>/g, '');
        for (var k in SAMPLE_VARS) r = r.split(k).join(SAMPLE_VARS[k]);
        return r;
    }

    function emojiImgHtml(emoji, cls) {
        if (!emoji) return '';
        if (emoji.indexOf('qrc:') === 0) {
            return '<img class="' + (cls||'') + '" src="' + emoji.replace('qrc:','') + '">';
        }
        // Unicode emoji - just show as text
        return '<span class="' + (cls||'') + '" style="font-size:1.1em">' + emoji + '</span>';
    }

    function renderItemVisual(item) {
        var bg = item.backgroundColor || '';
        var hasAction = (item.action||'') !== '' || (item.longPressAction||'') !== '' || (item.doubleclickAction||'') !== '';
        var bgStyle = bg || (hasAction ? '#555555' : 'var(--bg)');
        var html = '<div class="lib-entry-visual" style="background:' + bgStyle + '">';
        if (item.emoji) html += '<span class="lib-item-emoji">' + emojiImgHtml(item.emoji) + '</span>';
        var text = resolveVars(item.content || item.type || '');
        html += '<span class="lib-item-text">' + escapeHtml(text) + '</span>';
        html += '</div>';
        return html;
    }

    function renderZoneVisual(data) {
        var items = data.items || [];
        var html = '<div class="lib-entry-visual" style="background:var(--bg)">';
        html += '<div class="lib-zone-chips">';
        for (var i = 0; i < items.length; i++) {
            var it = items[i];
            var bg = it.backgroundColor || '';
            var hasAct = (it.action||'') !== '';
            var chipBg = bg || (hasAct ? '#555555' : 'var(--surface)');
            html += '<span class="lib-zone-mini-chip" style="background:' + chipBg + '">';
            if (it.emoji) html += emojiImgHtml(it.emoji);
            if (it.type !== 'custom') {
                html += (DISPLAY_NAMES[it.type] || it.type);
            } else {
                var t = resolveVars(it.content || '');
                html += escapeHtml(t.length > 10 ? t.substring(0,8)+'..' : (t||'Custom'));
            }
            html += '</span>';
        }
        html += '</div></div>';
        return html;
    }

    function renderLayoutVisual(data) {
        var layout = data.layout || {};
        var zones = layout.zones || {};
        var count = 0;
        for (var z in zones) count += zones[z].length;
        var html = '<div class="lib-entry-visual" style="background:var(--bg);flex-wrap:wrap">';
        // Show a mini summary of all zones
        for (var zn in zones) {
            var zItems = zones[zn];
            if (!zItems.length) continue;
            for (var j = 0; j < Math.min(zItems.length, 4); j++) {
                var it = zItems[j];
                var bg = it.backgroundColor || '';
                var hasAct = (it.action||'') !== '';
                var chipBg = bg || (hasAct ? '#555555' : 'var(--surface)');
                html += '<span class="lib-zone-mini-chip" style="background:' + chipBg + '">';
                if (it.type !== 'custom') html += (DISPLAY_NAMES[it.type] || it.type);
                else {
                    var t = resolveVars(it.content || '');
                    html += escapeHtml(t.length > 6 ? t.substring(0,5)+'..' : (t||'C'));
                }
                html += '</span>';
            }
            if (zItems.length > 4) html += '<span class="lib-zone-mini-chip" style="background:var(--surface)">+' + (zItems.length-4) + '</span>';
        }
        html += '</div>';
        return html;
    }

    function renderEntryCard(entry, id, isLocal) {
        var sel = id === libSelectedId ? ' selected' : '';
        var onclick = isLocal ? "selectLibEntry('" + id + "')" : "selectCommEntry('" + id + "')";
        var html = '<div class="lib-entry' + sel + '" onclick="' + onclick + '">';

        // Type badge overlay
        html += '<span class="lib-type-overlay ' + (entry.type||'') + '">' + (entry.type||'?') + '</span>';

        // Check for server thumbnail (community)
        var thumbUrl = entry.thumbnailFullUrl || '';
        if (thumbUrl) {
            html += '<div class="lib-entry-visual" style="background:var(--bg);justify-content:center">';
            html += '<img class="lib-thumb" src="' + thumbUrl + '">';
            html += '</div>';
        } else if (isLocal) {
            // Check for local thumbnail
            html += '<img class="lib-thumb" src="/api/library/thumbnail?id=' + id + '" style="display:none" onload="this.style.display=\'\';this.nextElementSibling.style.display=\'none\'">';
        }

        // Visual preview (shown if no thumbnail, or as fallback)
        var showFallback = !thumbUrl;
        if (showFallback && entry.data) {
            var fallbackId = 'lf_' + id.replace(/[^a-zA-Z0-9]/g,'_');
            var wrap = isLocal ? ' id="' + fallbackId + '"' : '';
            if (entry.type === 'item' && entry.data.item) {
                html += '<div' + wrap + '>' + renderItemVisual(entry.data.item) + '</div>';
            } else if (entry.type === 'zone' && entry.data.items) {
                html += '<div' + wrap + '>' + renderZoneVisual(entry.data) + '</div>';
            } else if (entry.type === 'layout') {
                html += '<div' + wrap + '>' + renderLayoutVisual(entry.data) + '</div>';
            }
        }

        html += '</div>';
        return html;
    }
)HTML";
    html += R"HTML(
    function renderLocalEntries() {
        var el = document.getElementById('libLocalEntries');
        if (!libLocalData.length) {
            el.innerHTML = '<div class="lib-empty">No saved entries yet.<br>Select a widget and click "+ Item" to save it.</div>';
            return;
        }
        var html = '';
        for (var i = 0; i < libLocalData.length; i++) {
            var e = libLocalData[i];
            html += renderEntryCard(e, e.id, true);
        }
        el.innerHTML = html;
    }

    function selectLibEntry(id) {
        libSelectedId = libSelectedId === id ? null : id;
        if (libCurrentTab === 'local') renderLocalEntries();
        else renderCommunityEntries();
        updateLibButtons();
    }

    function updateLibButtons() {
        var hasSelection = !!libSelectedId;
        var applyBtn = document.getElementById('libApplyBtn');
        var deleteBtn = document.getElementById('libDeleteBtn');
        var uploadBtn = document.getElementById('libUploadBtn');
        var commApplyBtn = document.getElementById('commApplyBtn');
        var commDownloadBtn = document.getElementById('commDownloadBtn');
        if (applyBtn) applyBtn.disabled = !hasSelection;
        if (deleteBtn) deleteBtn.disabled = !hasSelection;
        if (uploadBtn) uploadBtn.disabled = !hasSelection;
        if (commApplyBtn) commApplyBtn.disabled = !hasSelection;
        if (commDownloadBtn) commDownloadBtn.disabled = !hasSelection;
    }

    function saveToLibrary(type) {
        if (type === 'item') {
            if (!selectedChip || !selectedChip.id) { showLibToast('Select a widget first'); return; }
            apiPost('/api/library/save-item', {itemId: selectedChip.id}, function(r) {
                if (r.success) { showLibToast('Item saved to library'); loadLibrary(); }
                else showLibToast(r.error || 'Failed to save');
            });
        } else if (type === 'zone') {
            if (!selectedChip) { showLibToast('Select a zone first'); return; }
            apiPost('/api/library/save-zone', {zone: selectedChip.zone}, function(r) {
                if (r.success) { showLibToast('Zone saved to library'); loadLibrary(); }
                else showLibToast(r.error || 'Failed to save');
            });
        } else if (type === 'layout') {
            apiPost('/api/library/save-layout', {}, function(r) {
                if (r.success) { showLibToast('Layout saved to library'); loadLibrary(); }
                else showLibToast(r.error || 'Failed to save');
            });
        }
    }

    function applyFromLibrary() {
        if (!libSelectedId) return;
        var entry = null;
        var list = libCurrentTab === 'local' ? libLocalData : libCommunityData;
        for (var i = 0; i < list.length; i++) {
            if (list[i].id === libSelectedId) { entry = list[i]; break; }
        }
        if (!entry) return;

        // For community entries, download first then apply
        if (libCurrentTab === 'community') {
            downloadAndApply(entry);
            return;
        }

        // Layout applies globally, item/zone apply to the selected chip's zone
        if (entry.type === 'layout') {
            apiPost('/api/library/apply', {entryId: libSelectedId}, function(r) {
                if (r.success) { showLibToast('Layout applied'); loadLayout(); }
                else showLibToast(r.error || 'Failed to apply');
            });
        } else {
            if (!selectedChip) { showLibToast('Select a zone to apply to'); return; }
            var zone = selectedChip.zone;
            apiPost('/api/library/apply', {entryId: libSelectedId, zone: zone}, function(r) {
                if (r.success) { showLibToast(entry.type + ' applied to ' + zone); loadLayout(); }
                else showLibToast(r.error || 'Failed to apply');
            });
        }
    }

    function deleteFromLibrary() {
        if (!libSelectedId) return;
        if (!confirm('Delete this library entry?')) return;
        apiPost('/api/library/delete', {entryId: libSelectedId}, function(r) {
            if (r.success) {
                showLibToast('Entry deleted');
                libSelectedId = null;
                loadLibrary();
                updateLibButtons();
            } else showLibToast(r.error || 'Failed to delete');
        });
    }

    // Community
    function browseCommunity() {
        commPage = 1;
        var type = document.getElementById('commTypeFilter').value;
        var search = document.getElementById('commSearchInput').value;
        var sort = document.getElementById('commSortFilter').value;
        var url = '/api/community/browse?page=' + commPage + '&sort=' + encodeURIComponent(sort);
        if (type) url += '&type=' + encodeURIComponent(type);
        if (search) url += '&search=' + encodeURIComponent(search);

        showLibSpinner('Browsing community...');

        fetch(url).then(function(r){return r.json()}).then(function(data) {
            hideLibSpinner();
            if (data.error) {
                document.getElementById('libCommunityEntries').innerHTML = '<div class="lib-empty">' + escapeHtml(data.error) + '</div>';
                return;
            }
            libCommunityData = data.entries || [];
            commTotal = data.total || 0;
            renderCommunityEntries();
        }).catch(function() {
            hideLibSpinner();
            document.getElementById('libCommunityEntries').innerHTML = '<div class="lib-empty">Failed to load community entries.</div>';
        });
    }

    function loadMoreCommunity() {
        commPage++;
        var type = document.getElementById('commTypeFilter').value;
        var search = document.getElementById('commSearchInput').value;
        var sort = document.getElementById('commSortFilter').value;
        var url = '/api/community/browse?page=' + commPage + '&sort=' + encodeURIComponent(sort);
        if (type) url += '&type=' + encodeURIComponent(type);
        if (search) url += '&search=' + encodeURIComponent(search);

        fetch(url).then(function(r){return r.json()}).then(function(data) {
            if (data.entries) {
                libCommunityData = libCommunityData.concat(data.entries);
                commTotal = data.total || commTotal;
                renderCommunityEntries();
            }
        });
    }

    function renderCommunityEntries() {
        var el = document.getElementById('libCommunityEntries');
        if (!libCommunityData.length) {
            el.innerHTML = '<div class="lib-empty">No community entries found.</div>';
            document.getElementById('commLoadMore').style.display = 'none';
            return;
        }
        var html = '';
        for (var i = 0; i < libCommunityData.length; i++) {
            var e = libCommunityData[i];
            var id = e.serverId || e.id || '';
            html += renderEntryCard(e, id, false);
        }
        el.innerHTML = html;
        document.getElementById('commLoadMore').style.display = libCommunityData.length < commTotal ? '' : 'none';
    }

    function selectCommEntry(id) {
        libSelectedId = libSelectedId === id ? null : id;
        renderCommunityEntries();
        updateLibButtons();
    }

    function downloadOnly() {
        if (!libSelectedId) return;
        var entry = null;
        for (var i = 0; i < libCommunityData.length; i++) {
            var eid = libCommunityData[i].serverId || libCommunityData[i].id;
            if (eid === libSelectedId) { entry = libCommunityData[i]; break; }
        }
        if (!entry) return;
        var serverId = entry.serverId || entry.id;
        showLibSpinner('Downloading...');
        apiPost('/api/community/download', {serverId: serverId}, function(r) {
            if (r.success) { showLibToast('Downloaded to My Library'); loadLibrary(); }
            else showLibToast(r.error || 'Download failed');
        });
    }

    function downloadAndApply(entry) {
        var serverId = entry.serverId || entry.id;
        if (entry.type !== 'layout' && !selectedChip) {
            showLibToast('Select a zone to apply to');
            return;
        }
        var zone = selectedChip ? selectedChip.zone : '';
        showLibSpinner('Downloading & applying...');
        apiPost('/api/community/download', {serverId: serverId}, function(r) {
            if (r.success && r.localEntryId) {
                var localId = r.localEntryId;
                apiPost('/api/library/apply', {entryId: localId, zone: zone}, function(r2) {
                    if (r2.success) { showLibToast('Applied!'); loadLayout(); }
                    else showLibToast(r2.error || 'Failed to apply');
                });
                loadLibrary();
            } else showLibToast(r.error || 'Download failed');
        });
    }

    function uploadToComm() {
        if (!libSelectedId) return;
        if (!confirm('Share this entry to the community?')) return;
        showLibSpinner('Uploading...');
        apiPost('/api/community/upload', {entryId: libSelectedId}, function(r) {
            if (r.success) showLibToast('Shared to community!');
            else showLibToast(r.error || 'Upload failed');
        });
    }

    function showLibSpinner(msg) {
        document.getElementById('libSpinnerText').textContent = msg || 'Loading...';
        document.getElementById('libSpinner').classList.add('active');
    }
    function hideLibSpinner() {
        document.getElementById('libSpinner').classList.remove('active');
    }

    function showLibToast(msg) {
        hideLibSpinner();
        var el = document.getElementById('libToast');
        el.textContent = msg;
        el.classList.add('show');
        clearTimeout(window._toastTimer);
        window._toastTimer = setTimeout(function() { el.classList.remove('show'); }, 3000);
    }

    // Initial load
    loadLayout();
    loadLibrary();

    </script>
</body>
</html>
)HTML";

    return html;
}
