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

    // All remaining endpoints are POST
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
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            margin-bottom: 1rem;
        }
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
            background: var(--accent);
            color: #000;
            border-color: var(--accent);
        }
        .chip.special { color: orange; }
        .chip.selected.special { color: #000; }
        .chip-arrow {
            cursor: pointer;
            font-size: 1rem;
            opacity: 0.8;
        }
        .chip-arrow:hover { opacity: 1; }
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
            width: 120px;
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
            width: 120px;
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
            flex: 0 0 90px;
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
    </style>
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

                <!-- ROW 3: Format/Color | Variables | Actions -->
                <div class="editor-tools-row">
                    <div class="editor-tools-format">
                        <div class="color-row">
                            <span class="color-label">Color</span>
                            <div class="color-swatch" id="textColorSwatch" style="background:#ffffff" onclick="document.getElementById('textColorInput').click()"></div>
                            <input type="color" id="textColorInput" value="#ffffff" style="position:absolute;visibility:hidden;width:0;height:0" onchange="applyTextColor(this.value)">
                            <span class="color-swatch-x" onclick="clearTextColor()" title="Reset text color">&#10005;</span>
                            <span class="color-label" style="margin-left:6px">Bg</span>
                            <div class="color-swatch" id="bgColorSwatch" style="background:transparent" onclick="document.getElementById('bgColorInput').click()">
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
                    <button class="btn btn-cancel" onclick="closeEditor()">Cancel</button>
                    <button class="btn btn-save" onclick="saveText()">Save</button>
                </div>
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
        {id:"command:sleep",label:"Sleep"},
        {id:"command:startEspresso",label:"Start Espresso"},
        {id:"command:startSteam",label:"Start Steam"},
        {id:"command:startHotWater",label:"Start Hot Water"},
        {id:"command:startFlush",label:"Start Flush"},
        {id:"command:idle",label:"Stop (Idle)"},
        {id:"command:tare",label:"Tare Scale"}
    ];

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

            html += '<div class="zone-card" style="' + (isPairStart || isPairEnd ? 'flex:1' : '') + '">';
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
    function chipClick(itemId, zone, type) {
        if (selectedChip && selectedChip.id === itemId) {
            // Deselect
            selectedChip = null;
        } else if (selectedChip && selectedChip.zone !== zone) {
            // Move to different zone
            apiPost("/api/layout/move", {itemId: selectedChip.id, fromZone: selectedChip.zone, toZone: zone, toIndex: -1}, function() {
                selectedChip = null;
                loadLayout();
            });
            return;
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

    function openEditor(itemId, zone) {
        editingItem = {id: itemId, zone: zone};
        document.getElementById("emojiPickerArea").style.display = "none";
        document.getElementById("emojiToggleBtn").textContent = "Pick Icon";
        fetch("/api/layout/item?id=" + encodeURIComponent(itemId))
            .then(function(r){return r.json()})
            .then(function(props) {
                wysiwygEl.innerHTML = props.content || "Text";
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
        editingItem = null;
        document.getElementById("editorPanel").classList.add("editor-hidden");
    }

    function saveText() {
        if (!editingItem) return;
        var content = wysiwygEl.innerHTML || "Text";
        // Clean up browser contenteditable artifacts
        content = content.replace(/<div>/g, "<br>").replace(/<\/div>/g, "");
        if (content === "<br>") content = "Text";
        var id = editingItem.id;
        var done = 0;
        var total = 7;
        function check() { done++; if (done >= total) { itemPropsCache[id] = null; loadLayout(); } }
        apiPost("/api/layout/item", {itemId: id, key: "content", value: content}, check);
        apiPost("/api/layout/item", {itemId: id, key: "align", value: currentAlign}, check);
        apiPost("/api/layout/item", {itemId: id, key: "action", value: currentAction}, check);
        apiPost("/api/layout/item", {itemId: id, key: "longPressAction", value: currentLongPressAction}, check);
        apiPost("/api/layout/item", {itemId: id, key: "doubleclickAction", value: currentDoubleclickAction}, check);
        apiPost("/api/layout/item", {itemId: id, key: "emoji", value: currentEmoji}, check);
        apiPost("/api/layout/item", {itemId: id, key: "backgroundColor", value: currentBgColor}, check);
    }

    // ---- WYSIWYG formatting (execCommand) ----

    function execBold() {
        restoreSelection();
        document.execCommand("bold", false, null);
        saveSelection();
        updatePreview();
    }

    function execItalic() {
        restoreSelection();
        document.execCommand("italic", false, null);
        saveSelection();
        updatePreview();
    }

    function execFontSize(px) {
        restoreSelection();
        var sel = window.getSelection();
        if (!sel.rangeCount || sel.isCollapsed) return;
        // Wrap selection in span with font-size
        var range = sel.getRangeAt(0);
        var span = document.createElement("span");
        span.style.fontSize = px + "px";
        range.surroundContents(span);
        sel.collapseToEnd();
        saveSelection();
        updatePreview();
    }

    function applyTextColor(color) {
        restoreSelection();
        document.execCommand("foreColor", false, color);
        saveSelection();
        updateTextColorUI(color);
        updatePreview();
    }

    function clearTextColor() {
        restoreSelection();
        document.execCommand("removeFormat", false, null);
        saveSelection();
        updateTextColorUI("#ffffff");
        updatePreview();
    }

    function updateTextColorUI(color) {
        document.getElementById("textColorSwatch").style.background = color;
        document.getElementById("textColorInput").value = color;
    }

    function insertVar(token) {
        restoreSelection();
        document.execCommand("insertText", false, token);
        saveSelection();
        updatePreview();
    }

    function setAlign(a) {
        currentAlign = a;
        wysiwygEl.style.textAlign = a;
        updateAlignButtons();
        updatePreview();
    }

    function updateAlignButtons() {
        ["Left","Center","Right"].forEach(function(d) {
            var btn = document.getElementById("align" + d);
            if (btn) btn.classList.toggle("active", currentAlign === d.toLowerCase());
        });
    }

    // ---- Background Color ----

    function setBgColor(color) {
        currentBgColor = color;
        updateBgColorUI();
        updatePreview();
    }

    function clearBgColor() {
        currentBgColor = "";
        updateBgColorUI();
        updatePreview();
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
    }

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
    }

    function clearEmoji() {
        currentEmoji = "";
        updateIconPreview();
        renderEmojiGrid();
        updatePreview();
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

    // ---- Dual Preview (Full + Bar, matching tablet) ----

    function updatePreview() {
        var rawHtml = wysiwygEl.innerHTML || "";
        var plainText = stripHtml(rawHtml);
        var previewText = substitutePreview(plainText);
        var hasAction = currentAction || currentLongPressAction || currentDoubleclickAction;
        var hasEmoji = currentEmoji !== "";
        var bgColor = currentBgColor || ((hasAction || hasEmoji) ? "#555555" : "");
        var textColor = (hasAction || hasEmoji || currentBgColor) ? "white" : "var(--text)";

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
        fullHtml += '<span class="pv-text" style="color:' + textColor + '">' + previewText + '</span>';
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
        barHtml += '<span class="pv-text" style="color:' + textColor + '">' + previewText + '</span>';
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
    wysiwygEl.addEventListener("input", updatePreview);

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
        var div = document.createElement("div");
        div.textContent = str;
        return div.innerHTML;
    }

    // Initial load
    loadLayout();

    </script>
</body>
</html>
)HTML";

    return html;
}
