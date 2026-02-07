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

void ShotServer::handleBackupManifest(QTcpSocket* socket)
{
    QJsonObject manifest;

    // Device and app info
    QString deviceName = QSysInfo::machineHostName();
    if (deviceName.isEmpty() || deviceName == "localhost") {
        // Android devices often don't have a proper hostname
        // Use a more descriptive name
        QString productName = QSysInfo::prettyProductName();
        if (!productName.isEmpty()) {
            deviceName = productName;
        } else {
            deviceName = QSysInfo::productType() + " device";
        }
    }
    manifest["deviceName"] = deviceName;
    manifest["platform"] = QSysInfo::productType();
    manifest["appVersion"] = QString(VERSION_STRING);

    // Settings info
    if (m_settings) {
        manifest["hasSettings"] = true;
        // Estimate settings size (serialized JSON)
        QJsonObject settingsJson = SettingsSerializer::exportToJson(m_settings, false);
        QByteArray settingsData = QJsonDocument(settingsJson).toJson(QJsonDocument::Compact);
        manifest["settingsSize"] = settingsData.size();
    } else {
        manifest["hasSettings"] = false;
        manifest["settingsSize"] = 0;
    }

    // Profiles info
    // Profiles are stored flat in externalProfilesPath() and/or fallbackPath()
    // NOT in /user/ or /downloaded/ subdirectories
    if (m_profileStorage) {
        QString extPath = m_profileStorage->externalProfilesPath();
        QString fallbackPath = m_profileStorage->fallbackPath();

        qDebug() << "ShotServer: Profile paths for backup manifest:";
        qDebug() << "  External path:" << extPath;
        qDebug() << "  Fallback path:" << fallbackPath;

        int profileCount = 0;
        qint64 profilesSize = 0;
        QSet<QString> seenFiles;  // Avoid counting duplicates

        // Check external storage
        if (!extPath.isEmpty()) {
            QDir extDir(extPath);
            qDebug() << "  External dir exists:" << extDir.exists();
            if (extDir.exists()) {
                QFileInfoList files = extDir.entryInfoList(QStringList() << "*.json", QDir::Files);
                for (const QFileInfo& fi : files) {
                    // Skip temp/internal files
                    if (!fi.fileName().startsWith("_")) {
                        seenFiles.insert(fi.fileName());
                        profileCount++;
                        profilesSize += fi.size();
                    }
                }
                qDebug() << "  External profile .json files found:" << profileCount;
            }
        }

        // Check fallback path (avoid duplicates)
        QDir fallbackDir(fallbackPath);
        qDebug() << "  Fallback dir exists:" << fallbackDir.exists();
        if (fallbackDir.exists()) {
            QFileInfoList files = fallbackDir.entryInfoList(QStringList() << "*.json", QDir::Files);
            int fallbackCount = 0;
            for (const QFileInfo& fi : files) {
                if (!fi.fileName().startsWith("_") && !seenFiles.contains(fi.fileName())) {
                    seenFiles.insert(fi.fileName());
                    profileCount++;
                    profilesSize += fi.size();
                    fallbackCount++;
                }
            }
            qDebug() << "  Fallback profile .json files found:" << fallbackCount;
        }

        qDebug() << "  Total profile count:" << profileCount;
        manifest["profileCount"] = profileCount;
        manifest["profilesSize"] = profilesSize;
    } else {
        qDebug() << "ShotServer: m_profileStorage is null, cannot enumerate profiles";
        manifest["profileCount"] = 0;
        manifest["profilesSize"] = 0;
    }

    // Shots info
    if (m_storage) {
        manifest["shotCount"] = m_storage->totalShots();
        QString dbPath = m_storage->databasePath();
        QFileInfo dbInfo(dbPath);
        manifest["shotsSize"] = dbInfo.exists() ? dbInfo.size() : 0;
    } else {
        manifest["shotCount"] = 0;
        manifest["shotsSize"] = 0;
    }

    // Personal media info
    if (m_screensaverManager) {
        manifest["mediaCount"] = m_screensaverManager->personalMediaCount();
        QString mediaDir = m_screensaverManager->personalMediaDirectory();
        qint64 mediaSize = 0;
        QDir dir(mediaDir);
        if (dir.exists()) {
            QFileInfoList files = dir.entryInfoList(QDir::Files);
            for (const QFileInfo& fi : files) {
                if (fi.fileName() != "index.json") {
                    mediaSize += fi.size();
                }
            }
        }
        manifest["mediaSize"] = mediaSize;
    } else {
        manifest["mediaCount"] = 0;
        manifest["mediaSize"] = 0;
    }

    sendJson(socket, QJsonDocument(manifest).toJson(QJsonDocument::Compact));
}

void ShotServer::handleBackupSettings(QTcpSocket* socket, bool includeSensitive)
{
    if (!m_settings) {
        sendResponse(socket, 500, "application/json", R"({"error":"Settings not available"})");
        return;
    }

    QJsonObject settingsJson = SettingsSerializer::exportToJson(m_settings, includeSensitive);
    sendJson(socket, QJsonDocument(settingsJson).toJson(QJsonDocument::Compact));
}

void ShotServer::handleBackupProfilesList(QTcpSocket* socket)
{
    if (!m_profileStorage) {
        sendResponse(socket, 500, "application/json", R"({"error":"Profile storage not available"})");
        return;
    }

    QJsonArray profiles;
    QSet<QString> seenFiles;

    // Add profiles from external storage
    QString extPath = m_profileStorage->externalProfilesPath();
    if (!extPath.isEmpty()) {
        QDir extDir(extPath);
        if (extDir.exists()) {
            QFileInfoList files = extDir.entryInfoList(QStringList() << "*.json", QDir::Files);
            for (const QFileInfo& fi : files) {
                if (!fi.fileName().startsWith("_")) {
                    seenFiles.insert(fi.fileName());
                    QJsonObject profile;
                    profile["category"] = "external";
                    profile["filename"] = fi.fileName();
                    profile["size"] = fi.size();
                    profiles.append(profile);
                }
            }
        }
    }

    // Add profiles from fallback path (avoid duplicates)
    QString fallbackPath = m_profileStorage->fallbackPath();
    QDir fallbackDir(fallbackPath);
    if (fallbackDir.exists()) {
        QFileInfoList files = fallbackDir.entryInfoList(QStringList() << "*.json", QDir::Files);
        for (const QFileInfo& fi : files) {
            if (!fi.fileName().startsWith("_") && !seenFiles.contains(fi.fileName())) {
                QJsonObject profile;
                profile["category"] = "fallback";
                profile["filename"] = fi.fileName();
                profile["size"] = fi.size();
                profiles.append(profile);
            }
        }
    }

    QJsonDocument doc(profiles);
    sendJson(socket, doc.toJson(QJsonDocument::Compact));
}

void ShotServer::handleBackupProfileFile(QTcpSocket* socket, const QString& category, const QString& filename)
{
    if (!m_profileStorage) {
        sendResponse(socket, 500, "application/json", R"({"error":"Profile storage not available"})");
        return;
    }

    QString basePath;
    if (category == "external") {
        basePath = m_profileStorage->externalProfilesPath();
    } else if (category == "fallback") {
        basePath = m_profileStorage->fallbackPath();
    } else {
        sendResponse(socket, 400, "application/json", R"({"error":"Invalid category"})");
        return;
    }

    QString filePath = basePath + "/" + filename;
    QFileInfo fi(filePath);

    // Security check: ensure file is within expected directory
    if (!fi.absoluteFilePath().startsWith(basePath) || !fi.exists()) {
        sendResponse(socket, 404, "application/json", R"({"error":"Profile not found"})");
        return;
    }

    sendFile(socket, filePath, "application/json");
}

void ShotServer::handleBackupMediaList(QTcpSocket* socket)
{
    if (!m_screensaverManager) {
        sendResponse(socket, 500, "application/json", R"({"error":"Screensaver manager not available"})");
        return;
    }

    QString mediaDir = m_screensaverManager->personalMediaDirectory();
    QDir dir(mediaDir);

    QJsonArray mediaFiles;

    if (dir.exists()) {
        QFileInfoList files = dir.entryInfoList(QDir::Files);
        for (const QFileInfo& fi : files) {
            QJsonObject media;
            media["filename"] = fi.fileName();
            media["size"] = fi.size();
            mediaFiles.append(media);
        }
    }

    QJsonDocument doc(mediaFiles);
    sendJson(socket, doc.toJson(QJsonDocument::Compact));
}

void ShotServer::handleBackupMediaFile(QTcpSocket* socket, const QString& filename)
{
    if (!m_screensaverManager) {
        sendResponse(socket, 500, "application/json", R"({"error":"Screensaver manager not available"})");
        return;
    }

    QString mediaDir = m_screensaverManager->personalMediaDirectory();
    QString filePath = mediaDir + "/" + filename;
    QFileInfo fi(filePath);

    // Security check: ensure file is within expected directory
    if (!fi.absoluteFilePath().startsWith(mediaDir) || !fi.exists()) {
        sendResponse(socket, 404, "application/json", R"({"error":"Media file not found"})");
        return;
    }

    // Determine content type based on extension
    QString ext = fi.suffix().toLower();
    QString contentType = "application/octet-stream";
    if (ext == "jpg" || ext == "jpeg") contentType = "image/jpeg";
    else if (ext == "png") contentType = "image/png";
    else if (ext == "gif") contentType = "image/gif";
    else if (ext == "mp4") contentType = "video/mp4";
    else if (ext == "mov") contentType = "video/quicktime";
    else if (ext == "webm") contentType = "video/webm";

    sendFile(socket, filePath, contentType);
}

// ============================================================================
// Full Backup Download/Restore
// ============================================================================

void ShotServer::handleBackupFull(QTcpSocket* socket)
{
    struct Entry {
        QByteArray name;
        QByteArray data;
    };
    QList<Entry> entries;

    // 1. Settings
    if (m_settings) {
        QJsonObject settingsJson = SettingsSerializer::exportToJson(m_settings, true);
        QByteArray settingsData = QJsonDocument(settingsJson).toJson(QJsonDocument::Indented);
        entries.append({"settings.json", settingsData});
    }

    // 2. Shots database
    if (m_storage) {
        m_storage->checkpoint();
        QString dbPath = m_storage->databasePath();
        QFile dbFile(dbPath);
        if (dbFile.open(QIODevice::ReadOnly)) {
            entries.append({"shots.db", dbFile.readAll()});
        }
    }

    // 3. Profiles (from both external and fallback paths)
    if (m_profileStorage) {
        QSet<QString> seenFiles;
        auto addProfilesFrom = [&](const QString& dirPath) {
            if (dirPath.isEmpty()) return;
            QDir dir(dirPath);
            if (!dir.exists()) return;
            QFileInfoList files = dir.entryInfoList(QStringList() << "*.json", QDir::Files);
            for (const QFileInfo& fi : files) {
                if (fi.fileName().startsWith("_")) continue;
                if (seenFiles.contains(fi.fileName())) continue;
                seenFiles.insert(fi.fileName());
                QFile f(fi.absoluteFilePath());
                if (f.open(QIODevice::ReadOnly)) {
                    QByteArray name = ("profiles/" + fi.fileName()).toUtf8();
                    entries.append({name, f.readAll()});
                }
            }
        };
        addProfilesFrom(m_profileStorage->externalProfilesPath());
        addProfilesFrom(m_profileStorage->fallbackPath());
    }

    // 4. Media files
    if (m_screensaverManager) {
        QString mediaDir = m_screensaverManager->personalMediaDirectory();
        QDir dir(mediaDir);
        if (dir.exists()) {
            QFileInfoList files = dir.entryInfoList(QDir::Files);
            for (const QFileInfo& fi : files) {
                if (fi.fileName() == "index.json") continue;
                QFile f(fi.absoluteFilePath());
                if (f.open(QIODevice::ReadOnly)) {
                    QByteArray name = ("media/" + fi.fileName()).toUtf8();
                    entries.append({name, f.readAll()});
                }
            }
        }
    }

    // Build binary archive
    QByteArray archiveData;
    QBuffer buffer(&archiveData);
    buffer.open(QIODevice::WriteOnly);

    // Magic "DCBK"
    buffer.write("DCBK", 4);

    // Version (uint32 LE)
    quint32 version = 1;
    buffer.write(reinterpret_cast<const char*>(&version), 4);

    // Entry count (uint32 LE)
    quint32 count = static_cast<quint32>(entries.size());
    buffer.write(reinterpret_cast<const char*>(&count), 4);

    // Each entry
    for (const Entry& e : entries) {
        quint32 nameLen = static_cast<quint32>(e.name.size());
        buffer.write(reinterpret_cast<const char*>(&nameLen), 4);
        buffer.write(e.name);
        quint64 dataLen = static_cast<quint64>(e.data.size());
        buffer.write(reinterpret_cast<const char*>(&dataLen), 8);
        buffer.write(e.data);
    }

    buffer.close();

    qDebug() << "ShotServer: Created backup archive with" << entries.size() << "entries,"
             << archiveData.size() << "bytes";

    // Send as download
    QString filename = QString("decenza_backup_%1.dcbackup")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd"));
    QByteArray extraHeaders = QString("Content-Disposition: attachment; filename=\"%1\"\r\n").arg(filename).toUtf8();
    sendResponse(socket, 200, "application/octet-stream", archiveData, extraHeaders);
}

QString ShotServer::generateRestorePage() const
{
    QString html;

    // Part 1: Head and base CSS
    html += R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Restore Backup - Decenza DE1</title>
    <style>
)HTML";
    html += WEB_CSS_VARIABLES;
    html += WEB_CSS_HEADER;
    html += WEB_CSS_MENU;

    // Part 2: Page-specific CSS
    html += R"HTML(
        :root {
            --success: #18c37e;
            --error: #f85149;
        }
        .upload-card {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 2rem;
            margin-bottom: 1.5rem;
        }
        .upload-zone {
            border: 2px dashed var(--border);
            border-radius: 8px;
            padding: 3rem 2rem;
            text-align: center;
            cursor: pointer;
            transition: all 0.2s;
        }
        .upload-zone:hover, .upload-zone.dragover {
            border-color: var(--accent);
            background: rgba(201, 162, 39, 0.05);
        }
        .upload-zone.uploading {
            border-color: var(--text-secondary);
            cursor: default;
        }
        .upload-icon { font-size: 3rem; margin-bottom: 1rem; }
        .upload-text { color: var(--text-secondary); margin-bottom: 0.5rem; }
        .upload-hint { color: var(--text-secondary); font-size: 0.875rem; }
        input[type="file"] { display: none; }
        .progress-bar {
            display: none;
            height: 8px;
            background: var(--border);
            border-radius: 4px;
            margin-top: 1.5rem;
            overflow: hidden;
        }
        .progress-fill {
            height: 100%%;
            background: var(--accent);
            width: 0%%;
            transition: width 0.3s;
        }
        .status-message {
            margin-top: 1rem;
            padding: 1rem;
            border-radius: 8px;
            display: none;
        }
        .status-message.success {
            display: block;
            background: rgba(24, 195, 126, 0.1);
            border: 1px solid var(--success);
            color: var(--success);
        }
        .status-message.error {
            display: block;
            background: rgba(248, 81, 73, 0.1);
            border: 1px solid var(--error);
            color: var(--error);
        }
        .status-message.processing {
            display: block;
            background: rgba(201, 162, 39, 0.1);
            border: 1px solid var(--accent);
            color: var(--accent);
        }
        .info-box {
            background: var(--surface);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1.5rem;
        }
        .info-box h4 {
            margin-bottom: 0.75rem;
            color: var(--accent);
        }
        .info-box ul {
            list-style: none;
            padding: 0;
        }
        .info-box li {
            padding: 0.25rem 0;
            color: var(--text-secondary);
            font-size: 0.875rem;
        }
        .info-box li::before {
            content: "\2022 ";
            color: var(--accent);
        }
    </style>
</head>
<body>
)HTML";

    // Part 3: Header with back button and menu
    html += R"HTML(
    <header class="header">
        <div class="header-content">
            <div style="display:flex;align-items:center;gap:1rem">
                <a href="/" class="back-btn">&larr;</a>
                <h1>Restore Backup</h1>
            </div>
            <div class="header-right">
)HTML";
    html += generateMenuHtml();
    html += R"HTML(
            </div>
        </div>
    </header>
)HTML";

    // Part 4: Main content
    html += R"HTML(
    <main class="container" style="max-width:800px">
        <div class="upload-card">
            <div class="upload-zone" id="uploadZone" onclick="document.getElementById('fileInput').click()">
                <div class="upload-icon">&#128229;</div>
                <div class="upload-text">Click or drag a .dcbackup file here</div>
                <div class="upload-hint">Restores settings, profiles, shots, and media</div>
            </div>
            <input type="file" id="fileInput" accept=".dcbackup" onchange="handleFile(this.files[0])">
            <div class="progress-bar" id="progressBar">
                <div class="progress-fill" id="progressFill"></div>
            </div>
            <div class="status-message" id="statusMessage"></div>
        </div>

        <div class="info-box">
            <h4>&#9432; How restore works</h4>
            <ul>
                <li>Settings will be overwritten with backup values</li>
                <li>Shot history will be merged (no duplicates)</li>
                <li>Profiles with the same name are skipped (not overwritten)</li>
                <li>Media with the same name is skipped (not overwritten)</li>
                <li>The app may need a restart for some settings to take effect</li>
            </ul>
        </div>
    </main>
)HTML";

    // Part 5: JavaScript
    html += R"HTML(
    <script>
)HTML";
    html += WEB_JS_MENU;
    html += R"HTML(
        var uploadZone = document.getElementById("uploadZone");
        var progressBar = document.getElementById("progressBar");
        var progressFill = document.getElementById("progressFill");
        var statusMessage = document.getElementById("statusMessage");

        uploadZone.addEventListener("dragover", function(e) {
            e.preventDefault();
            uploadZone.classList.add("dragover");
        });
        uploadZone.addEventListener("dragleave", function(e) {
            e.preventDefault();
            uploadZone.classList.remove("dragover");
        });
        uploadZone.addEventListener("drop", function(e) {
            e.preventDefault();
            uploadZone.classList.remove("dragover");
            if (e.dataTransfer.files.length > 0) {
                handleFile(e.dataTransfer.files[0]);
            }
        });

        function handleFile(file) {
            if (!file) return;
            if (!file.name.endsWith(".dcbackup")) {
                showStatus("error", "Please select a .dcbackup file");
                return;
            }

            uploadZone.classList.add("uploading");
            progressBar.style.display = "block";
            progressFill.style.width = "0%";
            showStatus("processing", "Uploading backup (" + formatSize(file.size) + ")...");

            var xhr = new XMLHttpRequest();
            xhr.open("POST", "/api/backup/restore", true);
            xhr.timeout = 600000;

            xhr.upload.onprogress = function(e) {
                if (e.lengthComputable) {
                    var pct = (e.loaded / e.total) * 100;
                    progressFill.style.width = pct + "%";
                    if (pct >= 100) {
                        showStatus("processing", "Processing backup... this may take a moment");
                    }
                }
            };

            xhr.onload = function() {
                uploadZone.classList.remove("uploading");
                if (xhr.status === 200) {
                    try {
                        var r = JSON.parse(xhr.responseText);
                        var parts = [];
                        if (r.settings) parts.push("Settings restored");
                        if (r.shotsImported) parts.push("Shots merged");
                        if (r.profilesImported > 0) parts.push(r.profilesImported + " profiles imported");
                        if (r.profilesSkipped > 0) parts.push(r.profilesSkipped + " profiles already existed");
                        if (r.mediaImported > 0) parts.push(r.mediaImported + " media imported");
                        if (r.mediaSkipped > 0) parts.push(r.mediaSkipped + " media already existed");
                        if (parts.length === 0) parts.push("Nothing to restore");
                        showStatus("success", "Restore complete: " + parts.join(", "));
                    } catch (e) {
                        showStatus("success", "Restore complete");
                    }
                } else {
                    try {
                        var err = JSON.parse(xhr.responseText);
                        showStatus("error", "Restore failed: " + (err.error || "Unknown error"));
                    } catch (e) {
                        showStatus("error", "Restore failed: " + (xhr.responseText || "Unknown error"));
                    }
                }
            };

            xhr.onerror = function() {
                uploadZone.classList.remove("uploading");
                showStatus("error", "Connection error. Check that the server is running.");
            };

            xhr.ontimeout = function() {
                uploadZone.classList.remove("uploading");
                showStatus("error", "Upload timed out. The backup file may be too large.");
            };

            xhr.setRequestHeader("Content-Type", "application/octet-stream");
            xhr.setRequestHeader("X-Filename", encodeURIComponent(file.name));
            xhr.send(file);
        }

        function formatSize(bytes) {
            if (bytes < 1024) return bytes + " B";
            if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
            return (bytes / (1024 * 1024)).toFixed(1) + " MB";
        }

        function showStatus(type, message) {
            statusMessage.className = "status-message " + type;
            statusMessage.textContent = message;
            statusMessage.style.display = "block";
        }
    </script>
</body>
</html>
)HTML";

    return html;
}

void ShotServer::handleBackupRestore(QTcpSocket* socket, const QString& tempFilePath, const QString& headers)
{
    Q_UNUSED(headers);

    QString tempPathToCleanup = tempFilePath;
    auto cleanupTempFile = [&tempPathToCleanup]() {
        if (!tempPathToCleanup.isEmpty() && QFile::exists(tempPathToCleanup)) {
            QFile::remove(tempPathToCleanup);
        }
    };

    QFile file(tempFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        sendResponse(socket, 500, "application/json", R"({"error":"Failed to open uploaded file"})");
        cleanupTempFile();
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    // Validate magic and minimum size
    if (data.size() < 12 || data.left(4) != "DCBK") {
        sendResponse(socket, 400, "application/json", R"({"error":"Invalid backup file. Expected a .dcbackup file."})");
        cleanupTempFile();
        return;
    }

    // Parse header
    const char* ptr = data.constData();
    quint32 version;
    memcpy(&version, ptr + 4, 4);
    quint32 entryCount;
    memcpy(&entryCount, ptr + 8, 4);

    if (version != 1) {
        sendResponse(socket, 400, "application/json",
            QString(R"({"error":"Unsupported backup version: %1"})").arg(version).toUtf8());
        cleanupTempFile();
        return;
    }

    if (entryCount > 100000) {
        sendResponse(socket, 400, "application/json", R"json({"error":"Backup file appears corrupt (too many entries)"})json");
        cleanupTempFile();
        return;
    }

    qDebug() << "ShotServer: Restoring backup with" << entryCount << "entries," << data.size() << "bytes";

    qint64 offset = 12;
    bool settingsRestored = false;
    bool shotsRestored = false;
    int profilesImported = 0;
    int profilesSkipped = 0;
    int mediaImported = 0;
    int mediaSkipped = 0;

    for (quint32 i = 0; i < entryCount; i++) {
        // Read name length
        if (offset + 4 > data.size()) {
            qWarning() << "ShotServer: Backup truncated at entry" << i << "(name length)";
            break;
        }
        quint32 nameLen;
        memcpy(&nameLen, ptr + offset, 4);
        offset += 4;

        if (nameLen > 10000 || offset + nameLen > data.size()) {
            qWarning() << "ShotServer: Backup truncated at entry" << i << "(name)";
            break;
        }
        QString name = QString::fromUtf8(ptr + offset, nameLen);
        offset += nameLen;

        // Read data length
        if (offset + 8 > data.size()) {
            qWarning() << "ShotServer: Backup truncated at entry" << i << "(data length)";
            break;
        }
        quint64 dataLen;
        memcpy(&dataLen, ptr + offset, 8);
        offset += 8;

        if (offset + static_cast<qint64>(dataLen) > data.size()) {
            qWarning() << "ShotServer: Backup truncated at entry" << i << "(data)";
            break;
        }
        QByteArray entryData(ptr + offset, static_cast<qsizetype>(dataLen));
        offset += static_cast<qint64>(dataLen);

        // Process entry by type
        if (name == "settings.json") {
            if (m_settings) {
                QJsonDocument doc = QJsonDocument::fromJson(entryData);
                if (!doc.isNull() && doc.isObject()) {
                    SettingsSerializer::importFromJson(m_settings, doc.object());
                    settingsRestored = true;
                    qDebug() << "ShotServer: Restored settings";
                }
            }
        }
        else if (name == "shots.db") {
            if (m_storage) {
                QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
                QString dbTempPath = tempDir + "/restore_shots_" +
                    QString::number(QDateTime::currentMSecsSinceEpoch()) + ".db";
                QFile dbFile(dbTempPath);
                if (dbFile.open(QIODevice::WriteOnly)) {
                    dbFile.write(entryData);
                    dbFile.close();
                    int beforeCount = m_storage->totalShots();
                    bool success = m_storage->importDatabase(dbTempPath, true);
                    if (success) {
                        m_storage->refreshTotalShots();
                        int imported = m_storage->totalShots() - beforeCount;
                        qDebug() << "ShotServer: Imported" << imported << "new shots";
                        shotsRestored = true;
                    }
                }
                QFile::remove(dbTempPath);
            }
        }
        else if (name.startsWith("profiles/")) {
            if (m_profileStorage) {
                QString filename = name.mid(9);  // Remove "profiles/"
                // Strip .json extension since profileExists/writeProfile add it
                QString profileName = filename;
                if (profileName.endsWith(".json", Qt::CaseInsensitive)) {
                    profileName = profileName.left(profileName.length() - 5);
                }
                if (m_profileStorage->profileExists(profileName)) {
                    profilesSkipped++;
                } else {
                    QString content = QString::fromUtf8(entryData);
                    if (m_profileStorage->writeProfile(profileName, content)) {
                        profilesImported++;
                        qDebug() << "ShotServer: Imported profile:" << profileName;
                    }
                }
            }
        }
        else if (name.startsWith("media/")) {
            if (m_screensaverManager) {
                QString filename = name.mid(6);  // Remove "media/"
                if (filename == "index.json") continue;
                if (m_screensaverManager->hasPersonalMediaWithName(filename)) {
                    mediaSkipped++;
                } else {
                    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
                    QString mediaTempPath = tempDir + "/restore_media_" +
                        QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + filename;
                    QFile mediaFile(mediaTempPath);
                    if (mediaFile.open(QIODevice::WriteOnly)) {
                        mediaFile.write(entryData);
                        mediaFile.close();
                        if (m_screensaverManager->addPersonalMedia(mediaTempPath, filename)) {
                            mediaImported++;
                            qDebug() << "ShotServer: Imported media:" << filename;
                        }
                    }
                    QFile::remove(mediaTempPath);
                }
            }
        }
    }

    qDebug() << "ShotServer: Restore complete - settings:" << settingsRestored
             << "shots:" << shotsRestored
             << "profiles:" << profilesImported << "(skipped:" << profilesSkipped << ")"
             << "media:" << mediaImported << "(skipped:" << mediaSkipped << ")";

    // Build response
    QJsonObject result;
    result["success"] = true;
    result["settings"] = settingsRestored;
    result["shotsImported"] = shotsRestored;
    result["profilesImported"] = profilesImported;
    result["profilesSkipped"] = profilesSkipped;
    result["mediaImported"] = mediaImported;
    result["mediaSkipped"] = mediaSkipped;

    sendJson(socket, QJsonDocument(result).toJson(QJsonDocument::Compact));
    cleanupTempFile();
}
