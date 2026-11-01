#include "core/diagnosticlogging.h"
#include "shotserver.h"
#include "core/appsettings.h"
#include "webdebuglogger.h"
#include "webtemplates.h"
#include "../history/shothistorystorage.h"
#include "../ai/aiconversation.h"
#include "../core/dbutils.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"
#include "../screensaver/screensavervideomanager.h"
#include "../core/settings.h"
#include "../core/profilestorage.h"
#include "../core/settingsserializer.h"
#include "../core/translationmanager.h"
#include "../ai/aimanager.h"
#include "../core/fullbackuprestore.h"
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
#include <QUuid>
#include <QRegularExpression>
#include <QSettings>
#include <QThread>
#include <QSqlDatabase>
#include <QSqlQuery>

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
    manifest["hasLanguageTranslations"] = !m_translationManager.isNull();

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
    // Profiles can be in root, user/, or downloaded/ subdirectories
    if (m_profileStorage) {
        QString extPath = m_profileStorage->externalProfilesPath();
        QString fallbackPath = m_profileStorage->fallbackPath();

        DIAG_DEBUG(NETWORK, "ShotServer") << "Profile paths for backup manifest:";
        DIAG_DEBUG(NETWORK, "ShotServer") << "  External path:" << extPath;
        DIAG_DEBUG(NETWORK, "ShotServer") << "  Fallback path:" << fallbackPath;

        int profileCount = 0;
        qint64 profilesSize = 0;
        QSet<QString> seenFiles;  // Avoid counting duplicates (keyed by subdir/filename)

        // Helper to count profiles in a directory
        auto countProfiles = [&](const QString& basePath, const QString& subdir) {
            QString dirPath = subdir.isEmpty() ? basePath : basePath + "/" + subdir;
            QDir dir(dirPath);
            if (!dir.exists()) return;
            QFileInfoList files = dir.entryInfoList(QStringList() << "*.json", QDir::Files);
            for (const QFileInfo& fi : files) {
                if (!fi.fileName().startsWith("_")) {
                    QString key = (subdir.isEmpty() ? "" : subdir + "/") + fi.fileName();
                    if (!seenFiles.contains(key)) {
                        seenFiles.insert(key);
                        profileCount++;
                        profilesSize += fi.size();
                    }
                }
            }
        };

        // Check external storage (root, user/, downloaded/)
        if (!extPath.isEmpty()) {
            countProfiles(extPath, "");
            countProfiles(extPath, "user");
            countProfiles(extPath, "downloaded");
        }

        // Check fallback path (root, user/, downloaded/)
        countProfiles(fallbackPath, "");
        countProfiles(fallbackPath, "user");
        countProfiles(fallbackPath, "downloaded");

        DIAG_DEBUG(NETWORK, "ShotServer") << "  Total profile count:" << profileCount;
        manifest["profileCount"] = profileCount;
        manifest["profilesSize"] = profilesSize;
    } else {
        DIAG_DEBUG(NETWORK, "ShotServer") << "m_profileStorage is null, cannot enumerate profiles";
        manifest["profileCount"] = 0;
        manifest["profilesSize"] = 0;
    }

    // Shots info
    if (m_storage) {
        manifest["shotCount"] = m_storage->totalShots();
        QString dbPath = m_storage->databasePath();
        QFileInfo dbInfo(dbPath);
        manifest["shotsSize"] = dbInfo.exists() ? dbInfo.size() : 0;

        // Recipes / bags / equipment ride inside shots.db and transfer with the
        // Shots import — advertise their counts so the migration UI isn't
        // silent about them (finish-recipes-first-class). One quick read; the
        // tables may not exist on very old source DBs (COUNT then fails → 0).
        int recipeCount = 0, bagCount = 0, equipmentCount = 0;
        withTempDb(dbPath, "manifest_counts", [&](QSqlDatabase& db) {
            QSqlQuery q(db);
            if (q.exec("SELECT COUNT(*) FROM recipes") && q.next()) recipeCount = q.value(0).toInt();
            if (q.exec("SELECT COUNT(*) FROM coffee_bags") && q.next()) bagCount = q.value(0).toInt();
            if (q.exec("SELECT COUNT(*) FROM equipment_packages") && q.next()) equipmentCount = q.value(0).toInt();
        });
        manifest["recipeCount"] = recipeCount;
        manifest["bagCount"] = bagCount;
        manifest["equipmentCount"] = equipmentCount;
    } else {
        manifest["shotCount"] = 0;
        manifest["shotsSize"] = 0;
        manifest["recipeCount"] = 0;
        manifest["bagCount"] = 0;
        manifest["equipmentCount"] = 0;
    }

    // AI conversations info
    if (m_aiManager) {
        qsizetype convCount = m_aiManager->conversationIndex().size();
        manifest["aiConversationCount"] = convCount;
        // Estimate size: read messages JSON for each conversation
        qint64 aiSize = 0;
        AppSettings settings;
        for (const auto& entry : m_aiManager->conversationIndex()) {
            aiSize += settings.value("ai/conversations/" + entry.key + "/messages").toByteArray().size();
        }
        manifest["aiConversationsSize"] = aiSize;
    } else {
        manifest["aiConversationCount"] = 0;
        manifest["aiConversationsSize"] = 0;
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

    // Privacy: never emit raw credential VALUES (API keys, passwords, and the
    // visualizer/MQTT usernames) over the LAN web server, even when
    // includeSensitive=true is requested. This endpoint is UNauthenticated when
    // webSecurityEnabled is off (the default), so the parameter is ignored and
    // exportToJson is always called with includeSensitive=false. Trade-off: a settings
    // backup/restore or device migration taken via the web endpoint carries only
    // non-credential settings; all credentials (keys, passwords, usernames) must be
    // re-entered on the target. (The native on-device backup path is likewise
    // credential-free today — it also passes includeSensitive=false.)
    Q_UNUSED(includeSensitive);
    QJsonObject settingsJson = SettingsSerializer::exportToJson(m_settings, /*includeSensitive=*/false);
    sendJson(socket, QJsonDocument(settingsJson).toJson(QJsonDocument::Compact));
}

// Extra QSettings not covered by SettingsSerializer: shot-map location,
// accessibility preferences, and language. Shared by the full-archive backup
// (extra_settings.json) and the /api/backup/extra-settings LAN endpoint so the
// two transfer paths stay in parity (finish-recipes-first-class).
QJsonObject ShotServer::buildExtraSettingsObject()
{
    AppSettings settings;
    QJsonObject extra;

    QJsonObject shotMap;
    shotMap["manualCity"] = settings.value("shotMap/manualCity", "").toString();
    shotMap["manualLat"] = settings.value("shotMap/manualLat", 0.0).toDouble();
    shotMap["manualLon"] = settings.value("shotMap/manualLon", 0.0).toDouble();
    shotMap["manualCountryCode"] = settings.value("shotMap/manualCountryCode", "").toString();
    shotMap["manualGeocoded"] = settings.value("shotMap/manualGeocoded", false).toBool();
    extra["shotMap"] = shotMap;

    QJsonObject accessibility;
    accessibility["enabled"] = settings.value("accessibility/enabled", false).toBool();
    accessibility["ttsEnabled"] = settings.value("accessibility/ttsEnabled", true).toBool();
    accessibility["tickEnabled"] = settings.value("accessibility/tickEnabled", true).toBool();
    accessibility["tickSoundIndex"] = settings.value("accessibility/tickSoundIndex", 1).toInt();
    accessibility["tickVolume"] = settings.value("accessibility/tickVolume", 100).toInt();
    accessibility["extractionAnnouncementsEnabled"] = settings.value("accessibility/extractionAnnouncementsEnabled", true).toBool();
    accessibility["extractionAnnouncementInterval"] = settings.value("accessibility/extractionAnnouncementInterval", 5).toInt();
    accessibility["extractionAnnouncementMode"] = settings.value("accessibility/extractionAnnouncementMode", "both").toString();
    extra["accessibility"] = accessibility;

    extra["language"] = settings.value("localization/language", "en").toString();
    return extra;
}

void ShotServer::handleBackupExtraSettings(QTcpSocket* socket)
{
    QJsonObject extra = buildExtraSettingsObject();
    if (!m_translationManager) {
        sendJson(socket, QJsonDocument(extra).toJson(QJsonDocument::Compact));
        return;
    }
    const auto snapshot = m_translationManager->captureFileSnapshot();
    QPointer<QTcpSocket> socketGuard(socket);
    QPointer<ShotServer> serverGuard(this);
    QCoreApplication* const application = QCoreApplication::instance();
    if (!application) return;
    QThread* thread = QThread::create([serverGuard, application, extra, snapshot, socketGuard]() mutable {
        TranslationManager::addLanguageToBackup(extra, snapshot);
        const QByteArray payload = QJsonDocument(extra).toJson(QJsonDocument::Compact);
        QMetaObject::invokeMethod(application, [serverGuard, socketGuard, payload] {
            if (serverGuard && socketGuard) serverGuard->sendJson(socketGuard, payload);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ShotServer::handleBackupProfilesList(QTcpSocket* socket)
{
    if (!m_profileStorage) {
        sendResponse(socket, 500, "application/json", R"({"error":"Profile storage not available"})");
        return;
    }

    QJsonArray profiles;
    QSet<QString> seenFiles;  // Keyed by subdir/filename to avoid duplicates

    // Helper to add profiles from a directory
    auto addProfiles = [&](const QString& basePath, const QString& category, const QString& subdir) {
        QString dirPath = subdir.isEmpty() ? basePath : basePath + "/" + subdir;
        QDir dir(dirPath);
        if (!dir.exists()) return;
        QFileInfoList files = dir.entryInfoList(QStringList() << "*.json", QDir::Files);
        for (const QFileInfo& fi : files) {
            if (!fi.fileName().startsWith("_")) {
                QString key = (subdir.isEmpty() ? "" : subdir + "/") + fi.fileName();
                if (!seenFiles.contains(key)) {
                    seenFiles.insert(key);
                    QJsonObject profile;
                    // Category encodes both storage type and subdirectory for download
                    profile["category"] = subdir.isEmpty() ? category : category + "/" + subdir;
                    profile["filename"] = fi.fileName();
                    profile["size"] = fi.size();
                    profiles.append(profile);
                }
            }
        }
    };

    // Add profiles from external storage (root, user/, downloaded/)
    QString extPath = m_profileStorage->externalProfilesPath();
    if (!extPath.isEmpty()) {
        addProfiles(extPath, "external", "");
        addProfiles(extPath, "external", "user");
        addProfiles(extPath, "external", "downloaded");
    }

    // Add profiles from fallback path (root, user/, downloaded/)
    QString fallbackPath = m_profileStorage->fallbackPath();
    addProfiles(fallbackPath, "fallback", "");
    addProfiles(fallbackPath, "fallback", "user");
    addProfiles(fallbackPath, "fallback", "downloaded");

    QJsonDocument doc(profiles);
    sendJson(socket, doc.toJson(QJsonDocument::Compact));
}

void ShotServer::handleBackupProfileFile(QTcpSocket* socket, const QString& category, const QString& filename)
{
    if (!m_profileStorage) {
        sendResponse(socket, 500, "application/json", R"({"error":"Profile storage not available"})");
        return;
    }

    // Category can be "external", "fallback", "external/user", "external/downloaded",
    // "fallback/user", or "fallback/downloaded"
    QString basePath;
    QString storageType = category.section('/', 0, 0);  // "external" or "fallback"
    QString subdir = category.section('/', 1);           // "", "user", or "downloaded"

    if (storageType == "external") {
        basePath = m_profileStorage->externalProfilesPath();
    } else if (storageType == "fallback") {
        basePath = m_profileStorage->fallbackPath();
    } else {
        sendResponse(socket, 400, "application/json", R"({"error":"Invalid category"})");
        return;
    }

    // Only allow known subdirectories
    if (!subdir.isEmpty() && subdir != "user" && subdir != "downloaded") {
        sendResponse(socket, 400, "application/json", R"({"error":"Invalid category"})");
        return;
    }

    QString dirPath = subdir.isEmpty() ? basePath : basePath + "/" + subdir;
    QString filePath = dirPath + "/" + filename;
    QFileInfo fi(filePath);

    // Security check: ensure file is within expected directory
    if (!fi.absoluteFilePath().startsWith(dirPath) || !fi.exists()) {
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

QJsonArray ShotServer::serializeAIConversations() const
{
    if (!m_aiManager)
        return QJsonArray();

    // One producer, shared with the archive writer and matched field-for-field
    // by the importer. It reads the same `ai/conversations/index` AIManager
    // holds in memory, and AIManager persists that index on every change.
    AppSettings settings;
    return AIConversation::exportConversationsStatic(settings);
}

void ShotServer::handleBackupAIConversations(QTcpSocket* socket)
{
    QJsonArray conversations = serializeAIConversations();
    sendJson(socket, QJsonDocument(conversations).toJson(QJsonDocument::Compact));
}

// ============================================================================
// Full Backup Download/Restore
// ============================================================================

void ShotServer::handleBackupFull(QTcpSocket* socket)
{
    if (m_backupFullInProgress) {
        sendResponse(socket, 429, "text/plain", "Backup already in progress");
        return;
    }
    m_backupFullInProgress = true;

    struct Entry {
        QByteArray name;
        QByteArray data;
    };

    // Phase 1: Collect data that requires QObject access on the main thread
    QList<Entry> mainThreadEntries;

    // 1. Settings (requires m_settings QObject)
    if (m_settings) {
        QJsonObject settingsJson = SettingsSerializer::exportToJson(m_settings, false);
        QByteArray settingsData = QJsonDocument(settingsJson).toJson(QJsonDocument::Indented);
        mainThreadEntries.append({"settings.json", settingsData});
    }

    // 2. AI conversations (requires m_aiManager QObject)
    {
        QJsonArray conversations = serializeAIConversations();
        if (!conversations.isEmpty()) {
            QByteArray convData = QJsonDocument(conversations).toJson(QJsonDocument::Compact);
            mainThreadEntries.append({"ai_conversations.json", convData});
        }
    }

    // 3. Extra QSettings data (not in Settings class) — shared with the
    // /api/backup/extra-settings LAN endpoint via buildExtraSettingsObject().
    {
        QByteArray extraData = QJsonDocument(buildExtraSettingsObject()).toJson(QJsonDocument::Compact);
        mainThreadEntries.append({"extra_settings.json", extraData});
    }

    // Capture file paths for background-thread I/O
    QString dbPath = m_storage ? m_storage->databasePath() : QString();
    QStringList profileDirs;
    if (m_profileStorage) {
        QString extPath = m_profileStorage->externalProfilesPath();
        if (!extPath.isEmpty()) {
            profileDirs << extPath << extPath + "/user" << extPath + "/downloaded";
        }
        QString fbPath = m_profileStorage->fallbackPath();
        profileDirs << fbPath << fbPath + "/user" << fbPath + "/downloaded";
    }
    QString mediaDir = m_screensaverManager ? m_screensaverManager->personalMediaDirectory() : QString();
    QString backupDate = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    const bool hasLanguageSnapshot = m_translationManager;
    const auto languageSnapshot = hasLanguageSnapshot ? m_translationManager->captureFileSnapshot()
                                                      : TranslationManager::FileSnapshot{};

    // Phase 2: Run file I/O and archive building on background thread
    QPointer<QTcpSocket> socketGuard(socket);
    QPointer<ShotServer> serverGuard(this);
    QCoreApplication* const application = QCoreApplication::instance();
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, ownedMainThreadEntries = std::move(mainThreadEntries),
                                       dbPath, profileDirs, mediaDir, backupDate, hasLanguageSnapshot, languageSnapshot,
                                       socketGuard, serverGuard, application, destroyed]() {
        QList<Entry> entries = ownedMainThreadEntries;

        if (hasLanguageSnapshot) {
            for (Entry& entry : entries) {
                if (entry.name != "extra_settings.json") continue;
                QJsonObject extra = QJsonDocument::fromJson(entry.data).object();
                TranslationManager::addLanguageToBackup(extra, languageSnapshot);
                if (extra.value(QStringLiteral("languageTranslationError")).toBool()) {
                    if (application) QMetaObject::invokeMethod(application, [serverGuard, socketGuard] {
                        if (!serverGuard) return;
                        serverGuard->m_backupFullInProgress = false;
                        if (socketGuard) serverGuard->sendResponse(socketGuard, 500, "text/plain",
                            "Could not back up language customizations");
                    }, Qt::QueuedConnection);
                    return;
                }
                entry.data = QJsonDocument(extra).toJson(QJsonDocument::Compact);
                break;
            }
        }

        // 2. Shots database (checkpoint via temporary connection, then read file)
        if (!dbPath.isEmpty()) {
            withTempDb(dbPath, "shs_backup", [&](QSqlDatabase& db) {
                QSqlQuery walQuery(db);
                walQuery.exec("PRAGMA wal_checkpoint(FULL)");
            });

            QFile dbFile(dbPath);
            if (dbFile.open(QIODevice::ReadOnly)) {
                entries.append({"shots.db", dbFile.readAll()});
            }
        }

        // 3. Profiles
        {
            QSet<QString> seenFiles;
            for (const QString& dirPath : profileDirs) {
                QDir dir(dirPath);
                if (!dir.exists()) continue;
                // Determine subdir relative to base (e.g., "user", "downloaded", or "")
                QString subdir;
                if (dirPath.endsWith("/user")) subdir = "user";
                else if (dirPath.endsWith("/downloaded")) subdir = "downloaded";

                QFileInfoList files = dir.entryInfoList(QStringList() << "*.json", QDir::Files);
                for (const QFileInfo& fi : files) {
                    if (fi.fileName().startsWith("_")) continue;
                    QString key = (subdir.isEmpty() ? "" : subdir + "/") + fi.fileName();
                    if (seenFiles.contains(key)) continue;
                    seenFiles.insert(key);
                    QFile f(fi.absoluteFilePath());
                    if (f.open(QIODevice::ReadOnly)) {
                        QByteArray name = ("profiles/" + key).toUtf8();
                        entries.append({name, f.readAll()});
                    }
                }
            }
        }

        // 4. Media files
        if (!mediaDir.isEmpty()) {
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

        buffer.write("DCBK", 4);
        quint32 version = 1;
        buffer.write(reinterpret_cast<const char*>(&version), 4);
        quint32 count = static_cast<quint32>(entries.size());
        buffer.write(reinterpret_cast<const char*>(&count), 4);

        for (const Entry& e : entries) {
            quint32 nameLen = static_cast<quint32>(e.name.size());
            buffer.write(reinterpret_cast<const char*>(&nameLen), 4);
            buffer.write(e.name);
            quint64 dataLen = static_cast<quint64>(e.data.size());
            buffer.write(reinterpret_cast<const char*>(&dataLen), 8);
            buffer.write(e.data);
        }
        buffer.close();

        DIAG_DEBUG(NETWORK, "ShotServer") << "Created backup archive with" << entries.size() << "entries,"
                 << archiveData.size() << "bytes";

        // Send response on main thread
        QMetaObject::invokeMethod(this, [this, socketGuard, destroyed,
                                         ownedArchiveData = std::move(archiveData), backupDate]() {
            m_backupFullInProgress = false;  // Always reset before destroyed check
            if (*destroyed) {
                DIAG_DEBUG(NETWORK, "ShotServer") << "Backup response dropped (server destroyed)";
                return;
            }
            if (!socketGuard) {
                DIAG_DEBUG(NETWORK, "ShotServer") << "Backup response dropped (socket disconnected)";
                return;
            }

            QString filename = QString("decenza_backup_%1.dcbackup").arg(backupDate);
            QByteArray extraHeaders = QString("Content-Disposition: attachment; filename=\"%1\"\r\n").arg(filename).toUtf8();
            sendResponse(socketGuard, 200, "application/octet-stream", ownedArchiveData, extraHeaders);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
    <title>Restore Backup - Decenza</title>
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
            height: 100%;
            background: var(--accent);
            width: 0%;
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
                <a href="/legacy/" class="back-btn">&larr;</a>
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
                <div class="upload-hint">Restores settings, profiles, shots, media, and AI conversations</div>
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
                <li>AI conversations are merged (existing ones are not overwritten)</li>
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
    html += WEB_JS_POWER_CONTROL;
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
                        if (r.aiConversationsImported > 0) parts.push(r.aiConversationsImported + " AI conversations imported");
                        // What the restore REFUSED. Without it the page shows
                        // only the survivors, so a run that dropped most of the
                        // threads reads as a clean success.
                        if (r.aiConversationsNote) parts.push(r.aiConversationsNote);
                        if (parts.length === 0) parts.push("Nothing to restore");
                        // A refused or failed shot import is an ERROR banner, not
                        // a line inside a green "Restore complete". Reporting it
                        // as a success is what let a user whose whole history was
                        // refused read the result as if it had worked.
                        //
                        // `success` is the server's own verdict and is the gate;
                        // shotsRefused/error only supply the wording, so the two
                        // representations cannot drift into disagreeing.
                        if (r.success === false) {
                            var why = r.shotsRefused || r.error || "the server did not say why";
                            showStatus("error", "Shot history was not restored: " + why
                                       + " (" + parts.join(", ") + ")");
                        } else {
                            showStatus("success", "Restore complete: " + parts.join(", "));
                        }
                    } catch (e) {
                        // Do NOT report success here. Everything above — including
                        // the refusal check — runs inside this try, so a parse
                        // failure or a renamed field used to land on a green
                        // "Restore complete" and undo the whole check one path over.
                        console.error("restore response could not be read:", e);
                        showStatus("error", "The restore finished but its result could not be read"
                                   + " — check Shot History before restoring again.");
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
    const auto cleanup = [tempFilePath]() {
        if (!tempFilePath.isEmpty()) QFile::remove(tempFilePath);
    };
    if (!m_fullBackupRestore) {
        sendResponse(socket, 500, "application/json", R"({"error":"Backup restore is unavailable"})");
        cleanup();
        return;
    }

    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QPointer<QTcpSocket> socketGuard(socket);
    QPointer<ShotServer> serverGuard(this);
    const auto connection = connect(m_fullBackupRestore, &FullBackupRestore::finished,
        m_fullBackupRestore,
        [requestId, socketGuard, serverGuard, cleanup](const QString& finishedId, bool,
                const QVariantMap& result, const QString&, int status) {
            if (finishedId != requestId) return;
            cleanup();
            if (!serverGuard || !socketGuard) return;
            serverGuard->sendResponse(socketGuard, status, "application/json",
                         QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact));
        });
    FullBackupRestore::Options options; // Legacy HTTP restore defaults to merge/all categories.
    if (!m_fullBackupRestore->restoreFile(requestId, tempFilePath, options)) {
        disconnect(connection);
        sendResponse(socket, 429, "application/json", R"({"error":"Backup already in progress"})");
        cleanup();
    }
}
