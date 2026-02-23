#include "shotserver.h"
#include "webtemplates.h"
#include "../ai/aimanager.h"

#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QUrl>
#include <QRegularExpression>

QString ShotServer::generateAIConversationsPage() const
{
    QString html = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>AI Conversations - Decenza DE1</title>
    <style>
        :root {
            --bg: #0d1117;
            --surface: #161b22;
            --surface-hover: #1f2937;
            --border: #30363d;
            --text: #e6edf3;
            --text-secondary: #8b949e;
            --accent: #c9a227;
            --success: #18c37e;
            --error: #e73249;
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
            max-width: 800px;
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
        .container { max-width: 800px; margin: 0 auto; padding: 1.5rem; }
        .card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-bottom: 1rem;
            overflow: hidden;
        }
        .card-body {
            padding: 1.25rem;
        }
        .card-title {
            font-size: 1rem;
            font-weight: 600;
            margin-bottom: 0.25rem;
        }
        .card-meta {
            font-size: 0.8125rem;
            color: var(--text-secondary);
            margin-bottom: 0.75rem;
        }
        .card-actions {
            display: flex;
            gap: 0.5rem;
        }
        .btn {
            padding: 0.5rem 1rem;
            border: 1px solid var(--border);
            border-radius: 6px;
            font-size: 0.8125rem;
            font-weight: 500;
            cursor: pointer;
            text-decoration: none;
            color: var(--text);
            background: var(--surface-hover);
            transition: all 0.15s;
        }
        .btn:hover {
            border-color: var(--accent);
            color: var(--accent);
        }
        .empty-state {
            text-align: center;
            padding: 3rem 1.5rem;
            color: var(--text-secondary);
        }
        .empty-state .icon {
            font-size: 3rem;
            margin-bottom: 1rem;
        }
        .empty-state p {
            font-size: 0.9375rem;
        }
)HTML";

    html += WEB_CSS_MENU;

    html += R"HTML(
    </style>
</head>
<body>
    <header class="header">
        <div class="header-content">
            <a href="/" class="back-btn">&larr;</a>
            <h1>AI Conversations</h1>
)HTML";

    html += generateMenuHtml();

    html += R"HTML(
        </div>
    </header>

    <div class="container">
        <div class="card" style="border-color: var(--accent); margin-bottom: 1.5rem;">
            <div class="card-body" style="font-size: 0.875rem; color: var(--text-secondary);">
                Need help with the AI Dialing Assistant? Use the <strong style="color:var(--text);">Get Help</strong> button on a conversation to open a GitHub issue with the conversation data pre-filled.
            </div>
        </div>
)HTML";

    if (!m_aiManager || m_aiManager->conversationIndex().isEmpty()) {
        html += R"HTML(
        <div class="empty-state">
            <div class="icon">&#129302;</div>
            <p>No AI conversations yet.</p>
            <p style="margin-top: 0.5rem;">Use the Dialing Assistant in the app to start a conversation.</p>
        </div>
)HTML";
    } else {
        QSettings settings;
        const auto conversations = m_aiManager->conversationIndex();
        for (const auto& entry : conversations) {
            // Build context label
            QStringList parts;
            if (!entry.beanBrand.isEmpty()) parts << entry.beanBrand.toHtmlEscaped();
            if (!entry.beanType.isEmpty()) parts << entry.beanType.toHtmlEscaped();
            QString label = parts.isEmpty() ? QStringLiteral("Unknown beans") : parts.join(" ");
            if (!entry.profileName.isEmpty())
                label += " / " + entry.profileName.toHtmlEscaped();

            // Read message count from QSettings
            QString prefix = "ai/conversations/" + entry.key + "/";
            QByteArray messagesJson = settings.value(prefix + "messages").toByteArray();
            int msgCount = 0;
            if (!messagesJson.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(messagesJson);
                if (doc.isArray()) msgCount = doc.array().size();
            }

            // Read timestamp
            QString timestamp = settings.value(prefix + "timestamp").toString();
            QString displayTime;
            if (!timestamp.isEmpty()) {
                QDateTime dt = QDateTime::fromString(timestamp, Qt::ISODate);
                if (dt.isValid())
                    displayTime = dt.toLocalTime().toString("yyyy-MM-dd hh:mm");
            }
            if (displayTime.isEmpty() && entry.timestamp > 0) {
                displayTime = QDateTime::fromSecsSinceEpoch(entry.timestamp).toString("yyyy-MM-dd hh:mm");
            }

            QString keyEscaped = entry.key.toHtmlEscaped();

            html += QStringLiteral(R"HTML(
        <div class="card">
            <div class="card-body">
                <div class="card-title">%1</div>
                <div class="card-meta">%2 messages &middot; %3</div>
                <div class="card-actions">
                    <a class="btn" href="/api/ai-conversation/%4/download?format=json">&#128190; JSON</a>
                    <a class="btn" href="/api/ai-conversation/%4/download?format=text">&#128196; Text</a>
                    <a class="btn" href="#" data-key="%4" data-label="%1" onclick="openHelpIssue(this.dataset.key, this.dataset.label); return false;">&#128172; Get Help</a>
                </div>
            </div>
        </div>
)HTML").arg(label)
       .arg(msgCount)
       .arg(displayTime.isEmpty() ? QStringLiteral("Unknown date") : displayTime)
       .arg(keyEscaped);
        }
    }

    html += R"HTML(
    </div>
)HTML";

    html += R"HTML(
    <script>
)HTML";
    html += WEB_JS_MENU;
    html += R"HTML(
        function openHelpIssue(key, label) {
            // Step 1: Trigger JSON file download so user has it ready
            var a = document.createElement("a");
            a.href = "/api/ai-conversation/" + key + "/download?format=json";
            a.download = "";
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);

            // Step 2: Open pre-populated GitHub issue
            var title = "AI Dialing Assistant: " + label;
            var body = "**Describe your issue:**\n\n\n\n"
                + "---\n"
                + "**Please drag and drop the JSON file (just downloaded) into this issue.**\n";
            var url = "https://github.com/Kulitorum/Decenza/issues/new?"
                + "title=" + encodeURIComponent(title)
                + "&body=" + encodeURIComponent(body);
            window.open(url, "_blank");
        }
    </script>
)HTML";

    html += R"HTML(
</body>
</html>
)HTML";

    return html;
}

void ShotServer::handleAIConversationDownload(QTcpSocket* socket, const QString& key, const QString& format)
{
    if (key.isEmpty()) {
        sendResponse(socket, 400, "text/plain", "Missing conversation key");
        return;
    }

    QSettings settings;
    QString prefix = "ai/conversations/" + key + "/";

    // Read conversation data directly from QSettings
    QString systemPrompt = settings.value(prefix + "systemPrompt").toString();
    QByteArray messagesJson = settings.value(prefix + "messages").toByteArray();
    QString timestamp = settings.value(prefix + "timestamp").toString();

    if (messagesJson.isEmpty()) {
        sendResponse(socket, 404, "text/plain", "Conversation not found");
        return;
    }

    QJsonParseError parseError;
    QJsonDocument msgDoc = QJsonDocument::fromJson(messagesJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !msgDoc.isArray()) {
        sendResponse(socket, 500, "text/plain", "Corrupted conversation data");
        return;
    }

    QJsonArray messages = msgDoc.array();

    // Find conversation metadata from index
    QString beanBrand, beanType, profileName;
    if (m_aiManager) {
        for (const auto& entry : m_aiManager->conversationIndex()) {
            if (entry.key == key) {
                beanBrand = entry.beanBrand;
                beanType = entry.beanType;
                profileName = entry.profileName;
                break;
            }
        }
    }

    // Build context label for filename
    QStringList parts;
    if (!beanBrand.isEmpty()) parts << beanBrand;
    if (!beanType.isEmpty()) parts << beanType;
    if (!profileName.isEmpty()) parts << profileName;
    QString contextLabel = parts.isEmpty() ? QStringLiteral("AI Conversation") : parts.join(" - ");

    // Sanitize filename
    QString safeFilename = contextLabel;
    safeFilename.replace(QRegularExpression("[^a-zA-Z0-9 _-]"), "_");
    safeFilename = safeFilename.simplified().replace(' ', '_');
    if (safeFilename.length() > 80) safeFilename = safeFilename.left(80);

    if (format == "text") {
        // Plain text format
        QString text;
        text += "AI Conversation: " + contextLabel + "\n";
        text += "Date: " + timestamp + "\n";
        text += "Messages: " + QString::number(messages.size()) + "\n";
        text += QString(60, '=') + "\n\n";

        if (!systemPrompt.isEmpty()) {
            text += "[System Prompt]\n";
            text += systemPrompt + "\n\n";
            text += QString(60, '-') + "\n\n";
        }

        for (const QJsonValue& val : messages) {
            QJsonObject msg = val.toObject();
            QString role = msg["role"].toString();
            QString content = msg["content"].toString();

            if (role == "user")
                text += "[User]\n";
            else if (role == "assistant")
                text += "[Assistant]\n";
            else
                text += "[" + role + "]\n";

            text += content + "\n\n";
        }

        QByteArray body = text.toUtf8();
        QString filename = safeFilename + ".txt";
        QByteArray headers = "Content-Disposition: attachment; filename=\"" + filename.toUtf8() + "\"\r\n";
        sendResponse(socket, 200, "text/plain; charset=utf-8", body, headers);
    } else {
        // JSON format
        QJsonObject root;

        QJsonObject metadata;
        metadata["beanBrand"] = beanBrand;
        metadata["beanType"] = beanType;
        metadata["profileName"] = profileName;
        metadata["timestamp"] = timestamp;
        metadata["messageCount"] = messages.size();
        root["metadata"] = metadata;

        root["systemPrompt"] = systemPrompt;
        root["messages"] = messages;

        QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Indented);
        QString filename = safeFilename + ".json";
        QByteArray headers = "Content-Disposition: attachment; filename=\"" + filename.toUtf8() + "\"\r\n";
        sendResponse(socket, 200, "application/json", body, headers);
    }
}
