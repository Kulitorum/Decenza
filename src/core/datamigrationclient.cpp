#include "core/diagnosticlogging.h"
#include "datamigrationclient.h"
#include "appsettings.h"
#include "settings.h"
#include "settingsserializer.h"
#include "fullbackuprestore.h"
#include "translationmanager.h"
#include "../network/localnetworkaccess.h"
#include "profilestorage.h"
#include "../profile/profile.h"
#include "../profile/profilesavehelper.h"
#include "../history/shothistorystorage.h"
#include "../screensaver/screensavervideomanager.h"
#include "../ai/aimanager.h"
#include "../ai/aiconversation.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>
#include <QNetworkInterface>
#include <QTcpSocket>
#include <QHostAddress>
#include <QSslError>
#include <QSslConfiguration>
#include <QSettings>
#include <QSet>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QRunnable>
#include <QThreadPool>
#include <QUuid>

struct DataMigrationClient::FullBackupDownload {
    std::shared_ptr<QTemporaryDir> directory = std::make_shared<QTemporaryDir>();
    bool writing = false;
    bool failed = false;
};

DataMigrationClient::DataMigrationClient(QNetworkAccessManager* networkManager, QObject* parent)
    : QObject(parent)
    , m_networkManager(networkManager)
{
    Q_ASSERT(networkManager);
}

DataMigrationClient::~DataMigrationClient()
{
    stopDiscovery();
    cancel();
    delete m_tempDir;
}

void DataMigrationClient::setFullBackupRestore(FullBackupRestore* restore)
{
    m_fullRestore = restore;
}

void DataMigrationClient::setupSslHandling(QNetworkReply* reply)
{
    // Ignore SSL errors for self-signed certificates on LAN migration servers
    connect(reply, &QNetworkReply::sslErrors, this, [reply](const QList<QSslError>& errors) {
        DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Ignoring SSL errors for LAN migration:" << errors.size();
        reply->ignoreSslErrors();
    });
}

void DataMigrationClient::addSessionCookie(QNetworkRequest& request)
{
    if (!m_sessionToken.isEmpty()) {
        request.setRawHeader("Cookie", QString("decenza_session=%1").arg(m_sessionToken).toUtf8());
    }
}

void DataMigrationClient::saveSessionToken(const QString& serverHost, const QString& token)
{
    AppSettings settings;
    settings.beginGroup("migration_sessions");
    settings.setValue(serverHost, token);
    settings.endGroup();
}

QString DataMigrationClient::loadSessionToken(const QString& serverHost)
{
    AppSettings settings;
    settings.beginGroup("migration_sessions");
    QString token = settings.value(serverHost).toString();
    settings.endGroup();
    return token;
}

void DataMigrationClient::connectToServer(const QString& serverUrl)
{
    if (m_connecting || isImporting()) {
        return;
    }
    LocalNetworkAccess::request(LocalNetworkAccess::Feature::DeviceMigration);

    // Normalize URL
    m_serverUrl = serverUrl;
    if (!m_serverUrl.startsWith("http://") && !m_serverUrl.startsWith("https://")) {
        m_serverUrl = "http://" + m_serverUrl;
    }
    if (m_serverUrl.endsWith("/")) {
        m_serverUrl.chop(1);
    }

    if (!m_manifest.isEmpty()) {
        m_manifest.clear();
        emit manifestChanged();
    }
    m_connecting = true;
    m_errorMessage.clear();
    if (m_needsAuthentication) {
        m_needsAuthentication = false;
        emit needsAuthenticationChanged();
    }
    emit isConnectingChanged();
    emit serverUrlChanged();
    emit errorMessageChanged();

    setCurrentOperation(tr("Connecting..."));

    // Load cached session token for this server
    QUrl parsedUrl(m_serverUrl);
    m_connectHost = parsedUrl.host();
    m_connectPort = static_cast<quint16>(
        parsedUrl.port(parsedUrl.scheme() == QLatin1String("https") ? 443 : 80));
    m_sessionToken = loadSessionToken(m_connectHost);
    m_manifestRetriesLeft = MANIFEST_MAX_RETRIES;
    // A fresh connect supersedes any prior cancel/disconnect. Without this the
    // stale flag makes tryNextProbeCandidate() early-return, hanging the
    // preflight (m_connecting stuck true) on a reconnect to a multi-homed peer.
    m_cancelled = false;

    // Pick a reachable local interface before the HTTP flow. This is what makes
    // migration work on a host that is multi-homed on the peer's subnet, where
    // an unbound request can otherwise fail with EHOSTUNREACH. The preflight
    // calls fetchManifest() on success, or reports the peer unreachable.
    startReachabilityPreflight();
}

void DataMigrationClient::fetchManifest()
{
    QUrl url(m_serverUrl + "/api/backup/manifest");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza-Migration/1.0");
    addSessionCookie(request);

    m_currentReply = m_networkManager->get(request);
    setupSslHandling(m_currentReply);
    connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onManifestReply);
}

// ============================================================================
// Interface-aware reachability preflight
// ============================================================================

QList<QHostAddress> DataMigrationClient::gatherSubnetCandidates(const QHostAddress& target,
                                                                quint16 port) const
{
    QList<QPair<QHostAddress, int>> localV4;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) ||
            !(flags & QNetworkInterface::IsRunning) ||
            (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                localV4.append({entry.ip(), entry.prefixLength()});
            }
        }
    }

    // Which source address would the kernel pick for this target by default?
    // Connecting a UDP socket resolves the source without sending any traffic.
    QHostAddress preferred;
    QUdpSocket sourceProbe;
    sourceProbe.connectToHost(target, port ? port : quint16(9));
    if (sourceProbe.state() == QAbstractSocket::ConnectedState) {
        preferred = sourceProbe.localAddress();
    }
    sourceProbe.close();

    return orderedSubnetCandidates(target, localV4, preferred);
}

void DataMigrationClient::startReachabilityPreflight()
{
    const QHostAddress target(m_connectHost);

    // Only the ambiguous case needs a probe: the target is an IPv4 literal and
    // we hold more than one local address on its subnet (i.e. genuinely
    // multi-homed there). A hostname, a routed target, or a single/zero on-subnet
    // address has no ambiguity — keep the existing direct path, adding no probe
    // round-trips (an IPv4 literal still does a sub-millisecond local route
    // lookup here, but no TCP probe or per-candidate timeout).
    QList<QHostAddress> candidates;
    if (!target.isNull() && target.protocol() == QAbstractSocket::IPv4Protocol) {
        candidates = gatherSubnetCandidates(target, m_connectPort);
    }

    if (candidates.size() < 2) {
        fetchManifest();
        return;
    }

    DIAG_DEBUG(STORAGE, "DataMigrationClient") << "multi-homed on target subnet, probing"
             << candidates.size() << "candidate interfaces for" << m_connectHost;
    m_probeCandidates = candidates;
    m_probeIndex = 0;
    tryNextProbeCandidate();
}

void DataMigrationClient::tryNextProbeCandidate()
{
    if (m_cancelled) {
        return;
    }
    if (m_probeIndex >= m_probeCandidates.size()) {
        finishProbe(false);
        return;
    }

    const QHostAddress source = m_probeCandidates.at(m_probeIndex);

    teardownProbeSocket();
    m_probeSocket = new QTcpSocket(this);

    if (!m_probeTimer) {
        m_probeTimer = new QTimer(this);
        m_probeTimer->setSingleShot(true);
        connect(m_probeTimer, &QTimer::timeout, this, &DataMigrationClient::onProbeFailed);
    }

    connect(m_probeSocket, &QTcpSocket::connected, this, &DataMigrationClient::onProbeSucceeded);
    connect(m_probeSocket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { onProbeFailed(); });

    if (!m_probeSocket->bind(source)) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "could not bind probe to" << source.toString()
                   << "-" << m_probeSocket->errorString();
        onProbeFailed();
        return;
    }

    m_probeSocket->connectToHost(m_connectHost, m_connectPort);
    m_probeTimer->start(PROBE_TIMEOUT_MS);
}

void DataMigrationClient::onProbeSucceeded()
{
    if (!m_probeSocket) {
        return;
    }
    if (m_probeTimer) {
        m_probeTimer->stop();
    }
    const QHostAddress via = m_probeSocket->localAddress();
    teardownProbeSocket();
    DIAG_DEBUG(STORAGE, "DataMigrationClient") << "reachable via local source" << via.toString();
    finishProbe(true);
}

void DataMigrationClient::onProbeFailed()
{
    if (!m_probeSocket) {
        return;  // already torn down (e.g. connected and error both queued)
    }
    if (m_probeTimer) {
        m_probeTimer->stop();
    }
    const QHostAddress tried = m_probeIndex < m_probeCandidates.size()
                                   ? m_probeCandidates.at(m_probeIndex)
                                   : QHostAddress();
    DIAG_DEBUG(STORAGE, "DataMigrationClient") << "candidate source" << tried.toString()
             << "cannot reach" << m_connectHost;
    teardownProbeSocket();
    m_probeIndex++;
    tryNextProbeCandidate();
}

void DataMigrationClient::teardownProbeSocket()
{
    if (m_probeSocket) {
        m_probeSocket->disconnect(this);
        m_probeSocket->abort();
        m_probeSocket->deleteLater();
        m_probeSocket = nullptr;
    }
}

void DataMigrationClient::finishProbe(bool reachable)
{
    m_probeCandidates.clear();
    m_probeIndex = 0;

    if (!reachable) {
        m_connecting = false;
        emit isConnectingChanged();
        setError(tr("Connection failed: device is not reachable on your local network"));
        setCurrentOperation(tr("Connection failed"));
        emit connectionFailed(m_errorMessage);
        return;
    }

    fetchManifest();
}

void DataMigrationClient::disconnect()
{
    if (m_fullRestoreInFlight) return;
    cancel();
    m_serverUrl.clear();
    m_manifest.clear();
    emit serverUrlChanged();
    emit manifestChanged();
}

void DataMigrationClient::onManifestReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // Safety check: if reply doesn't match (stale signal from race condition)
    if (reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();

    // Retry transient network failures before declaring failure — a cold
    // neighbour on a multi-homed subnet can drop the first attempt. (When the
    // multi-homed preflight ran it already picked a reachable link; on the common
    // direct path this simply re-attempts the transient failure.) HTTP status
    // codes and parse errors mean the connection itself worked — never retried.
    if (statusCode != 401 && err != QNetworkReply::NoError &&
        isTransientNetworkError(err) && m_manifestRetriesLeft > 0) {
        m_manifestRetriesLeft--;
        const QString transientMsg = reply->errorString();
        reply->deleteLater();
        m_currentReply = nullptr;
        DIAG_DEBUG(STORAGE, "DataMigrationClient") << "transient manifest error (" << transientMsg
                 << ") — retrying," << m_manifestRetriesLeft << "left";
        fetchManifest();
        return;
    }

    m_connecting = false;
    emit isConnectingChanged();

    // Check for 401 (authentication required) — reachable, needs a code.
    if (statusCode == 401) {
        DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Server requires authentication (401)";
        reply->deleteLater();
        m_currentReply = nullptr;

        // Clear stale token from memory and persistent storage
        m_sessionToken.clear();
        QUrl parsedUrl(m_serverUrl);
        saveSessionToken(parsedUrl.host(), QString());

        m_needsAuthentication = true;
        emit needsAuthenticationChanged();
        setCurrentOperation(tr("Authentication required"));
        return;
    }

    if (err != QNetworkReply::NoError) {
        // Distinguish "could not reach the device at all" from "reached it, but
        // the connection dropped/errored" so the message is truthful. Keep the
        // raw error string in the reachability case too — UnknownNetworkError is
        // a Qt grab-bag (EHOSTUNREACH lands here, but so can other transport
        // faults), so the detail must stay visible rather than be replaced.
        const QString msg = isTransientNetworkError(err)
            ? tr("Connection failed: device is not reachable on your local network (%1)")
                  .arg(reply->errorString())
            : tr("Connection failed: %1").arg(reply->errorString());
        setError(msg);
        emit connectionFailed(m_errorMessage);
        reply->deleteLater();
        m_currentReply = nullptr;
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    m_currentReply = nullptr;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        setError(tr("Invalid response from server"));
        emit connectionFailed(m_errorMessage);
        return;
    }

    m_manifest = doc.object().toVariantMap();
    emit manifestChanged();

    setCurrentOperation(tr("Connected"));
    emit connected();

    DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Connected to" << m_serverUrl
             << "- Device:" << m_manifest["deviceName"].toString()
             << "- Profiles:" << m_manifest["profileCount"].toInt()
             << "- Shots:" << m_manifest["shotCount"].toInt()
             << "- Media:" << m_manifest["mediaCount"].toInt();
}

void DataMigrationClient::authenticate(const QString& totpCode)
{
    if (m_serverUrl.isEmpty()) {
        return;
    }

    m_connecting = true;
    m_errorMessage.clear();
    emit isConnectingChanged();
    emit errorMessageChanged();
    setCurrentOperation(tr("Authenticating..."));

    QUrl url(m_serverUrl + "/api/auth/login");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "Decenza-Migration/1.0");

    QJsonObject body;
    body["code"] = totpCode.trimmed();
    QByteArray postData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    m_currentReply = m_networkManager->post(request, postData);
    setupSslHandling(m_currentReply);
    connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onAuthReply);
}

void DataMigrationClient::onAuthReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    m_connecting = false;
    emit isConnectingChanged();

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = tr("Connection failed: %1").arg(reply->errorString());
        reply->deleteLater();
        m_currentReply = nullptr;
        setError(errorMsg);
        emit authenticationFailed(errorMsg);
        return;
    }

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data = reply->readAll();
    QString setCookie = QString::fromUtf8(reply->rawHeader("Set-Cookie"));
    reply->deleteLater();
    m_currentReply = nullptr;

    if (statusCode == 200) {
        // Extract session token from Set-Cookie header
        QRegularExpression re("decenza_session=([^;]+)");
        QRegularExpressionMatch match = re.match(setCookie);
        if (match.hasMatch()) {
            m_sessionToken = match.captured(1);

            // Persist the token
            QUrl parsedUrl(m_serverUrl);
            saveSessionToken(parsedUrl.host(), m_sessionToken);

            DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Authenticated successfully, session cached";
        }

        m_needsAuthentication = false;
        emit needsAuthenticationChanged();
        emit authenticationSucceeded();

        // Retry connecting now that we have a session
        connectToServer(m_serverUrl);
    } else {
        // Parse error from response
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QString errorMsg = tr("Authentication failed");
        if (doc.isObject() && doc.object().contains("error")) {
            errorMsg = doc.object()["error"].toString();
        }
        setError(errorMsg);
        emit authenticationFailed(errorMsg);
    }
}

void DataMigrationClient::startImport(const QStringList& types)
{
    if (isImporting() || m_serverUrl.isEmpty()) {
        return;
    }

    ++m_importGeneration;
    m_lastImportWasReplacement = false;
    m_importing = true;
    m_cancelled = false;
    m_settingsImported = 0;
    m_profilesImported = 0;
    m_shotsImported = 0;
    m_mediaImported = 0;
    m_aiConversationsImported = 0;
    setAiConversationNote(QString());
    // Reset with the other per-run counters. It is a member so the conversations
    // step can read the shots step's map, and a member that survives a run is
    // how a second import — importOnlyAIConversations(), or a reconnect to a
    // DIFFERENT source device — would remap this run's turns through the last
    // run's map. importDatabaseStatic may return before writing outResult, so
    // it cannot be relied on to overwrite this.
    m_shotImport = {};
    m_progress = 0.0;
    m_errorMessage.clear();

    emit isImportingChanged();
    emit progressChanged();
    emit errorMessageChanged();

    // Calculate total bytes for progress based on requested types
    m_totalBytes = 0;
    if (types.contains("settings")) {
        m_totalBytes += m_manifest["settingsSize"].toLongLong();
    }
    if (types.contains("profiles")) {
        m_totalBytes += m_manifest["profilesSize"].toLongLong();
    }
    if (types.contains("shots")) {
        m_totalBytes += m_manifest["shotsSize"].toLongLong();
    }
    if (types.contains("media")) {
        m_totalBytes += m_manifest["mediaSize"].toLongLong();
    }
    if (types.contains("ai_conversations")) {
        m_totalBytes += m_manifest["aiConversationsSize"].toLongLong();
    }
    m_receivedBytes = 0;

    // Set up the import queue
    m_importQueue = types;

    startNextImport();
}

void DataMigrationClient::importAll(bool replace)
{
    if (replace) {
        if (isImporting() || m_serverUrl.isEmpty()) return;
        m_fullBackupDownload = std::make_shared<FullBackupDownload>();
        if (!m_fullBackupDownload->directory->isValid()) {
            m_fullBackupDownload.reset();
            setError(tr("The downloaded backup could not be saved."));
            emit importFailed(m_errorMessage);
            return;
        }
        ++m_importGeneration;
        m_lastImportWasReplacement = true;
        m_cancelled = false;
        m_importing = true;
        m_errorMessage.clear();
        m_totalBytes = 0;
        m_receivedBytes = 0;
        setAiConversationNote(QString());
        setProgress(0.0);
        setCurrentOperation(tr("Downloading backup for replacement..."));
        emit errorMessageChanged();
        emit isImportingChanged();
        QNetworkRequest request{QUrl(m_serverUrl + "/api/backup/full")};
        addSessionCookie(request);
        m_currentReply = m_networkManager->get(request);
        m_currentReply->setReadBufferSize(64 * 1024);
        setupSslHandling(m_currentReply);
        connect(m_currentReply, &QNetworkReply::readyRead, this, &DataMigrationClient::pumpFullBackupDownload);
        connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onFullBackupReply);
        connect(m_currentReply, &QNetworkReply::downloadProgress, this, &DataMigrationClient::onDownloadProgress);
        return;
    }
    // Build list of all available data types
    QStringList types;
    if (m_manifest["hasSettings"].toBool()) {
        types << "settings";
    }
    if (m_manifest["profileCount"].toInt() > 0) {
        types << "profiles";
    }
    if (m_manifest["shotCount"].toInt() > 0 || m_manifest["recipeCount"].toInt() > 0
        || m_manifest["bagCount"].toInt() > 0 || m_manifest["equipmentCount"].toInt() > 0) {
        types << "shots";
    }
    if (m_manifest["mediaCount"].toInt() > 0) {
        types << "media";
    }
    if (m_manifest["aiConversationCount"].toInt() > 0) {
        types << "ai_conversations";
    }

    startImport(types);
}

void DataMigrationClient::onFullBackupReply()
{
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || reply != m_currentReply || m_cancelled) return;
    pumpFullBackupDownload();
}

void DataMigrationClient::pumpFullBackupDownload()
{
    const auto download = m_fullBackupDownload;
    QNetworkReply* reply = m_currentReply;
    if (!download || !reply || download->writing || m_cancelled) return;
    const QString path = download->directory->filePath(QStringLiteral("transfer.dcbackup"));
    const QByteArray chunk = download->failed ? QByteArray{} : reply->read(64 * 1024);
    if (!chunk.isEmpty()) {
        download->writing = true;
        const quint64 generation = m_importGeneration;
        const QPointer<DataMigrationClient> guard(this);
        QCoreApplication* const application = QCoreApplication::instance();
        if (!application) return;
        QThreadPool::globalInstance()->start(QRunnable::create([guard, application, download, path, chunk, generation] {
            QFile file(path);
            const bool saved = file.open(QIODevice::WriteOnly | QIODevice::Append)
                && file.write(chunk) == chunk.size() && file.flush();
            file.close();
            QMetaObject::invokeMethod(application, [guard, download, saved, generation] {
                if (!guard || guard->m_cancelled || guard->m_importGeneration != generation
                    || guard->m_fullBackupDownload != download) return;
                download->writing = false;
                download->failed = !saved;
                guard->pumpFullBackupDownload();
            }, Qt::QueuedConnection);
        }));
        return;
    }
    if (!reply->isFinished() && !download->failed) return;
    const bool failed = download->failed || reply->error() != QNetworkReply::NoError || !m_fullRestore;
    m_currentReply = nullptr;
    reply->deleteLater();
    m_fullBackupDownload.reset();
    if (failed) {
        m_importing = false;
        setError(tr("The backup could not be fetched or restored. Your existing data was not changed."));
        emit isImportingChanged();
        emit importFailed(m_errorMessage);
        return;
    }
    const auto directory = download->directory;
    const QPointer<DataMigrationClient> guard(this);
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_fullRestoreInFlight = true;
    setCurrentOperation(tr("Restoring backup..."));
    if (!guard || !m_fullRestore) return;
    emit isImportingChanged();
    if (!guard || !m_fullRestore) return;
    const auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(m_fullRestore.data(), &FullBackupRestore::finished, this,
        [guard, requestId, connection](const QString& id, bool success, const QVariantMap& result,
                                       const QString& reason, int) {
        if (id != requestId) return;
        QObject::disconnect(*connection);
        if (!guard) return;
        auto* client = guard.data();
        client->m_fullRestoreInFlight = false;
        client->m_importing = false;
        client->setAiConversationNote(result.value(QStringLiteral("aiConversationsNote")).toString());
        if (!guard) return;
        if (!success) client->setError(result.value(QStringLiteral("error"), reason).toString());
        else client->setProgress(1.0);
        if (!guard) return;
        emit client->isImportingChanged();
        if (!guard) return;
        if (!success) emit client->importFailed(client->m_errorMessage);
        else emit client->importComplete(result.value(QStringLiteral("settings")).toBool() ? 1 : 0,
            result.value(QStringLiteral("profilesImported")).toInt(),
            result.value(QStringLiteral("shotsImported")).toBool() ? 1 : 0,
            result.value(QStringLiteral("mediaImported")).toInt(),
            result.value(QStringLiteral("aiConversationsImported")).toInt());
    });
    FullBackupRestore::Options options;
    options.merge = false;
    options.preserveFlowCalibration = true;
    if (!m_fullRestore->restoreFile(requestId, path, options, directory)) {
        QObject::disconnect(*connection);
        m_fullRestoreInFlight = false;
        m_importing = false;
        setError(tr("The backup could not be restored while another operation is active."));
        emit isImportingChanged();
        emit importFailed(m_errorMessage);
    }
}

void DataMigrationClient::importOnlySettings()
{
    if (m_manifest["hasSettings"].toBool()) {
        startImport(QStringList{"settings"});
    }
}

void DataMigrationClient::importOnlyProfiles()
{
    if (m_manifest["profileCount"].toInt() > 0) {
        startImport(QStringList{"profiles"});
    }
}

void DataMigrationClient::importOnlyShots()
{
    if (m_manifest["shotCount"].toInt() > 0 || m_manifest["recipeCount"].toInt() > 0
        || m_manifest["bagCount"].toInt() > 0 || m_manifest["equipmentCount"].toInt() > 0) {
        startImport(QStringList{"shots"});
    }
}

void DataMigrationClient::importOnlyMedia()
{
    if (m_manifest["mediaCount"].toInt() > 0) {
        startImport(QStringList{"media"});
    }
}

void DataMigrationClient::importOnlyAIConversations()
{
    if (m_manifest["aiConversationCount"].toInt() > 0) {
        startImport(QStringList{"ai_conversations"});
    }
}

void DataMigrationClient::doImportAIConversations()
{
    setCurrentOperation(tr("Importing AI conversations..."));

    QUrl url(m_serverUrl + "/api/backup/ai-conversations");
    QNetworkRequest request(url);

    m_currentReply = m_networkManager->get(request);
    connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onAIConversationsReply);
    connect(m_currentReply, &QNetworkReply::downloadProgress, this, &DataMigrationClient::onDownloadProgress);
}

void DataMigrationClient::onAIConversationsReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (m_cancelled || reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        // Reported, not just logged: without this the run ends `clean` and the
        // dialog prints a green "Import complete" for a transfer that failed.
        // Same defect as the non-array case below — they are two arms of one if,
        // and only the second was fixed the first time.
        setErrorIfFirst(tr("The AI conversations could not be fetched from the other device."));
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to import AI conversations:" << reply->errorString();
    } else {
        QByteArray data = reply->readAll();
        m_receivedBytes += data.size();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isArray()) {
            // Not an array means the peer answered with something else — an
            // error page, a truncated body, an object. Silence here fell
            // through to a green "Import complete" with nothing imported.
            setErrorIfFirst(tr("The other device did not return a usable list of "
                               "AI conversations."));
            DIAG_WARN(STORAGE, "DataMigrationClient") << "AI conversations payload was not a JSON array ("
                       << data.size() << "bytes )";
        }
        if (doc.isArray()) {
            AppSettings settings;

            // Same importer the backup restore uses. This loop used to be a
            // second hand-written copy of it; the two were identical only by
            // luck, and the shotId remap below is exactly the kind of change
            // that would have landed in one and not the other.
            //
            // The map comes from the `shots` step of this same import run —
            // importAll() queues shots before ai_conversations, so by the time
            // we get here it is filled, and startImport() clears it at the top
            // of every run so a previous run's map can never stand in.
            //
            // A REFUSAL is its own case and must NOT import. The guards fire
            // on a transient condition, so the user's next move is to run the
            // migration again — and importing now with every id cleared makes
            // that retry useless, because the keys would already exist and the
            // retry skips them as duplicates.
            if (m_shotImport.refused()) {
                // Held back, and SAID so. The user is already being told the shot
                // history was refused; without this they are not told that their
                // conversations were held back with it, and the retry that
                // recovers both looks optional.
                setAiConversationNote(
                    AIConversation::importHeldBackNote(doc.array().size(), m_translationManager));
                DIAG_WARN(STORAGE, "DataMigrationClient") << "shot import was refused, so AI conversations"
                           << "were NOT imported — retry the migration rather than lose their"
                           << "shot links";
            } else {
                // No map means every turn's shotId is cleared: the user imported
                // conversations WITHOUT shots, or the shot import failed. Either
                // way the ids name the source device's database, which this
                // device does not have — keeping them is the bug. The equipment
                // packages arrive with the shots too, so a conversations-only
                // migration carries across only the unpackaged threads: the rest
                // name a package this device cannot identify.
                const AIConversation::ImportTally tally =
                    AIConversation::importConversationsStatic(settings, doc.array(),
                                                              m_shotImport.idMapOrNull(),
                                                              m_shotImport.equipmentIdMapOrNull());
                m_aiConversationsImported += tally.conversationsImported;
                setAiConversationNote(AIConversation::importRefusalNote(tally, m_translationManager));

                if (tally.conversationsImported > 0 && m_aiManager)
                    m_aiManager->reloadConversations();

                DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Imported" << m_aiConversationsImported
                         << "AI conversations;" << tally.turnsRemapped
                         << "shot reference(s) remapped," << tally.turnsCleared << "cleared";
            }
        }
    }

    reply->deleteLater();
    m_currentReply = nullptr;
    startNextImport();
}

void DataMigrationClient::startNextImport()
{
    if (m_cancelled) {
        m_importing = false;
        emit isImportingChanged();
        return;
    }

    if (m_importQueue.isEmpty()) {
        // All done. "Complete" only when nothing failed — the dialog renders
        // importComplete's summary in the success colour and errorMessage in
        // red, and printing both leaves the user reading "Shots were not
        // imported" directly above a green "Import complete".
        m_importing = false;
        setProgress(1.0);
        const bool clean = m_errorMessage.isEmpty();
        setCurrentOperation(clean ? tr("Import complete")
                                  : tr("Import finished with errors"));
        emit isImportingChanged();
        if (clean) {
            emit importComplete(m_settingsImported, m_profilesImported, m_shotsImported,
                                m_mediaImported, m_aiConversationsImported);
        } else {
            DIAG_WARN(STORAGE, "DataMigrationClient") << "import finished with errors -" << m_errorMessage;
            emit importFailed(m_errorMessage);
        }
        return;
    }

    QString next = m_importQueue.takeFirst();

    if (next == "settings") {
        doImportSettings();
    } else if (next == "profiles") {
        doImportProfiles();
    } else if (next == "shots") {
        doImportShots();
    } else if (next == "media") {
        doImportMedia();
    } else if (next == "ai_conversations") {
        doImportAIConversations();
    }
}

void DataMigrationClient::doImportSettings()
{
    setCurrentOperation(tr("Importing settings..."));

    QUrl url(m_serverUrl + "/api/backup/settings");
    QNetworkRequest request(url);
    addSessionCookie(request);

    m_currentReply = m_networkManager->get(request);
    setupSslHandling(m_currentReply);
    connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onSettingsReply);
    connect(m_currentReply, &QNetworkReply::downloadProgress, this, &DataMigrationClient::onDownloadProgress);
}

void DataMigrationClient::onSettingsReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // Safety check: if cancelled or reply doesn't match (stale signal from race condition)
    if (m_cancelled || reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to import settings:" << reply->errorString();
        setErrorIfFirst(tr("The settings could not be fetched from the other device."));
        // Continue with next import
    } else {
        QByteArray data = reply->readAll();
        m_receivedBytes += data.size();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject() && m_settings) {
            // The /api/backup/settings response omits all raw credential values
            // (the endpoint forces includeSensitive=false), so API keys, passwords,
            // and the visualizer/MQTT usernames are not transferred and must be
            // re-entered on the target device. Only non-credential settings transfer.
            // We additionally drop machine-specific flow calibration and defensively
            // re-exclude any mqttPassword key on import.
            if (SettingsSerializer::importFromJson(m_settings, doc.object(), {"flowCalibration", "mqttPassword"})) {
                m_settingsImported = 1;
                DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Settings imported successfully";
            } else {
                DIAG_WARN(STORAGE, "DataMigrationClient") << "Settings import returned failure";
                setErrorIfFirst(tr("The settings could not be applied."));
            }
        } else {
            setErrorIfFirst(doc.isObject() ? tr("The settings store is unavailable.")
                                          : tr("The other device did not return usable settings."));
        }
    }

    reply->deleteLater();
    m_currentReply = nullptr;
    // Chain the extra-settings fetch (shot-map location, accessibility,
    // language) so LAN migration reaches parity with the full archive — the
    // gap this closes. It advances the queue itself when done.
    doImportExtraSettings();
}

void DataMigrationClient::doImportExtraSettings()
{
    QUrl url(m_serverUrl + "/api/backup/extra-settings");
    QNetworkRequest request(url);
    addSessionCookie(request);

    m_currentReply = m_networkManager->get(request);
    setupSslHandling(m_currentReply);
    connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onExtraSettingsReply);
}

void DataMigrationClient::onExtraSettingsReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    if (m_cancelled || reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        DIAG_DEBUG(STORAGE, "DataMigrationClient") << "extra-settings not available:" << reply->errorString();
        reply->deleteLater();
        m_currentReply = nullptr;
        startNextImport();
        return;
    }
    const QByteArray data = reply->readAll();
    m_receivedBytes += data.size();
    reply->deleteLater();
    m_currentReply = nullptr;
    const QPointer<TranslationManager> manager(m_translationManager);
    const bool hasManager = !manager.isNull();
    const auto snapshot = hasManager ? manager->captureFileSnapshot() : TranslationManager::FileSnapshot{};
    const auto directory = std::make_shared<QTemporaryDir>();
    const quint64 generation = m_importGeneration;
    const QPointer<DataMigrationClient> guard(this);
    QCoreApplication* const application = QCoreApplication::instance();
    if (!directory->isValid() || !application) {
        setErrorIfFirst(tr("The language customizations could not be prepared."));
        startNextImport();
        return;
    }
    QThreadPool::globalInstance()->start(QRunnable::create(
        [guard, manager, hasManager, application, directory, data, snapshot, generation] {
        const QJsonDocument document = data.size() <= 16 * 1024 * 1024
            ? QJsonDocument::fromJson(data) : QJsonDocument();
        const QJsonObject extra = document.object();
        const bool hasLanguage = extra.contains(QStringLiteral("languageTranslation"));
        bool valid = document.isObject() && !extra.value(QStringLiteral("languageTranslationError")).toBool();
        QStringList overrides;
        TranslationManager::PreparedImport prepared;
        if (hasLanguage) {
            const QJsonObject language = extra.value(QStringLiteral("languageTranslation")).toObject();
            valid = valid && hasManager && !language.isEmpty()
                && TranslationManager::readBackupOverrides(language, extra.value(QStringLiteral("languageOverrides")), &overrides);
            const QByteArray payload = QJsonDocument(language).toJson(QJsonDocument::Compact);
            const QString path = directory->filePath(QStringLiteral("language.json"));
            QFile file(path);
            if (valid && file.open(QIODevice::WriteOnly) && file.write(payload) == payload.size() && file.flush()) {
                file.close();
                prepared = TranslationManager::prepareTranslationImport(snapshot, path,
                    extra.value(QStringLiteral("language")).toString());
                valid = prepared.reason.isEmpty();
            } else {
                valid = false;
            }
        }
        QMetaObject::invokeMethod(application, [guard, manager, directory, generation, extra, hasLanguage, valid, prepared, overrides] {
            Q_UNUSED(directory)
            if (!guard || guard->m_cancelled || guard->m_importGeneration != generation) return;
            if (!valid) {
                guard->setErrorIfFirst(guard->tr("The other device returned invalid language customizations."));
            } else if (hasLanguage && manager && manager->isAutoTranslating()) {
                guard->setErrorIfFirst(guard->tr("Language customizations cannot be imported while AI translation is running."));
            } else {
                AppSettings settings;
                const QJsonObject sm = extra.value(QStringLiteral("shotMap")).toObject();
                if (!sm.isEmpty()) {
                    settings.setValue("shotMap/manualCity", sm["manualCity"].toString());
                    settings.setValue("shotMap/manualLat", sm["manualLat"].toDouble());
                    settings.setValue("shotMap/manualLon", sm["manualLon"].toDouble());
                    settings.setValue("shotMap/manualCountryCode", sm["manualCountryCode"].toString());
                    settings.setValue("shotMap/manualGeocoded", sm["manualGeocoded"].toBool());
                }
                const QJsonObject accessibility = extra.value(QStringLiteral("accessibility")).toObject();
                const QStringList fields{QStringLiteral("enabled"), QStringLiteral("ttsEnabled"),
                    QStringLiteral("tickEnabled"), QStringLiteral("tickSoundIndex"), QStringLiteral("tickVolume"),
                    QStringLiteral("extractionAnnouncementsEnabled"), QStringLiteral("extractionAnnouncementInterval"),
                    QStringLiteral("extractionAnnouncementMode")};
                for (const QString& field : fields)
                    if (accessibility.contains(field))
                        settings.setValue(QStringLiteral("accessibility/") + field, accessibility.value(field).toVariant());
                if (extra.contains(QStringLiteral("language")))
                    settings.setValue(QStringLiteral("localization/language"), extra.value(QStringLiteral("language")).toString());
                if (hasLanguage) {
                    const bool imported = manager && manager->commitTranslationImport(prepared)
                        .value(QStringLiteral("success")).toBool();
                    if (!guard) return;
                    if (imported && manager) manager->setCurrentLanguage(prepared.language);
                    if (!guard) return;
                    if (!imported || !manager || manager->currentLanguage() != prepared.language
                        || !manager->applyTransferredOverrides(overrides))
                        guard->setErrorIfFirst(guard->tr("The language customizations could not be imported."));
                }
            }
            if (guard && !guard->m_cancelled && guard->m_importGeneration == generation)
                guard->startNextImport();
        }, Qt::QueuedConnection);
    }));
}

void DataMigrationClient::doImportProfiles()
{
    setCurrentOperation(tr("Fetching profile list..."));

    // First fetch the list of profiles
    QUrl url(m_serverUrl + "/api/backup/profiles");
    QNetworkRequest request(url);
    addSessionCookie(request);

    m_currentReply = m_networkManager->get(request);
    setupSslHandling(m_currentReply);
    connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onProfileListReply);
}

void DataMigrationClient::onProfileListReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // Safety check: if cancelled or reply doesn't match (stale signal from race condition)
    if (m_cancelled || reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to fetch profile list:" << reply->errorString();
        setErrorIfFirst(tr("The profiles could not be fetched from the other device."));
        reply->deleteLater();
        m_currentReply = nullptr;
        startNextImport();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    m_currentReply = nullptr;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Invalid profile list response";
        setErrorIfFirst(tr("The other device did not return a usable profile list."));
        startNextImport();
        return;
    }

    // Build list of profiles to download
    m_pendingProfiles.clear();
    QJsonArray profiles = doc.array();
    for (const QJsonValue& val : profiles) {
        QJsonObject obj = val.toObject();
        ProfileDownload pd;
        pd.category = obj["category"].toString();
        pd.filename = obj["filename"].toString();
        pd.size = obj["size"].toInt();
        m_pendingProfiles.append(pd);
    }

    DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Found" << m_pendingProfiles.size() << "profiles to download";
    downloadNextProfile();
}

void DataMigrationClient::downloadNextProfile()
{
    if (m_cancelled) {
        m_pendingProfiles.clear();
        startNextImport();
        return;
    }

    if (m_pendingProfiles.isEmpty()) {
        DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Imported" << m_profilesImported << "profiles";
        startNextImport();
        return;
    }

    ProfileDownload pd = m_pendingProfiles.takeFirst();
    setCurrentOperation(tr("Importing profile: %1").arg(pd.filename));

    // URL encode the filename
    QString encodedFilename = QUrl::toPercentEncoding(pd.filename);
    QUrl url(m_serverUrl + "/api/backup/profile/" + pd.category + "/" + encodedFilename);
    QNetworkRequest request(url);
    addSessionCookie(request);

    m_currentReply = m_networkManager->get(request);
    setupSslHandling(m_currentReply);
    m_currentReply->setProperty("category", pd.category);
    m_currentReply->setProperty("filename", pd.filename);
    connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onProfileFileReply);
    connect(m_currentReply, &QNetworkReply::downloadProgress, this, &DataMigrationClient::onDownloadProgress);
}

void DataMigrationClient::onProfileFileReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // Safety check: if cancelled or reply doesn't match (stale signal from race condition)
    if (m_cancelled || reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    QString category = reply->property("category").toString();
    QString filename = reply->property("filename").toString();

    if (reply->error() != QNetworkReply::NoError) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to download profile" << filename << ":" << reply->errorString();
        setErrorIfFirst(tr("A profile could not be fetched from the other device."));
    } else if (m_profileStorage) {
        QByteArray content = reply->readAll();
        m_receivedBytes += content.size();

        // Save to external storage if available, otherwise fallback
        // (the category just tells us where it came FROM, not where to save)
        Q_UNUSED(category)
        QString basePath = m_profileStorage->externalProfilesPath();
        if (basePath.isEmpty()) {
            basePath = m_profileStorage->fallbackPath();
        }

        QDir().mkpath(basePath);
        QString targetPath = basePath + "/" + filename;

        // Load the profile to clean it (strips "*" prefix) and check for duplicates
        Profile incomingProfile = Profile::loadFromJsonString(QString::fromUtf8(content));

        if (!incomingProfile.isValid()) {
            DIAG_WARN(STORAGE, "DataMigrationClient") << "Invalid profile, skipping:" << filename;
            setErrorIfFirst(tr("A profile from the other device is invalid."));
            reply->deleteLater();
            m_currentReply = nullptr;
            downloadNextProfile();
            return;
        }

        // Check if incoming profile would shadow a built-in profile
        bool shouldSkip = false;
        QString builtinPath = QStringLiteral(":/profiles/") + filename;
        if (QFile::exists(builtinPath)) {
            Profile builtIn = Profile::loadFromFile(builtinPath);
            if (builtIn.isValid() && ProfileSaveHelper::compareProfiles(incomingProfile, builtIn)) {
                // Identical to built-in — skip entirely
                DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Skipping profile identical to built-in:" << filename;
                shouldSkip = true;
            }
            // Different from built-in — force unique filename below
        }

        // Check if file already exists at target path
        if (!shouldSkip && QFile::exists(targetPath)) {
            Profile existingProfile = Profile::loadFromFile(targetPath);

            if (existingProfile.isValid() &&
                ProfileSaveHelper::compareProfiles(existingProfile, incomingProfile)) {
                // True duplicate — skip import
                DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Skipping duplicate profile:" << filename;
                shouldSkip = true;
            }

            // If not a true duplicate, append _imported suffix
            if (!shouldSkip) {
                QString baseName = QFileInfo(targetPath).completeBaseName();
                QString suffix = QFileInfo(targetPath).suffix();
                int counter = 1;
                do {
                    targetPath = QString("%1/%2_imported%3.%4")
                        .arg(basePath, baseName)
                        .arg(counter > 1 ? QString::number(counter) : "")
                        .arg(suffix);
                    counter++;
                } while (QFile::exists(targetPath));
            }
        }

        // Save the cleaned profile (with "*" stripped and any other normalization)
        if (!shouldSkip) {
            // A profile that failed to parse yields a default-constructed Profile
            // (title "Default", zero frames). Writing that produced a `"steps": []`
            // file — which stricter readers in the ecosystem reject outright — while
            // incrementing the success counter and logging "Imported profile:
            // Default", so a device-to-device migration reported success for a
            // profile nobody can open. Refuse it loudly instead.
            if (!incomingProfile.isValid()) {
                DIAG_WARN(STORAGE, "DataMigrationClient") << "Refusing to import invalid profile"
                           << incomingProfile.title() << "->" << targetPath
                           << "- no frames; source is malformed or unsupported";
                reply->deleteLater();
                m_currentReply = nullptr;
                downloadNextProfile();
                return;
            }
            if (incomingProfile.saveToFile(targetPath)) {
                m_profilesImported++;
                DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Imported profile:" << incomingProfile.title();
            } else {
                DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to save profile:" << targetPath;
                setErrorIfFirst(tr("A profile could not be saved."));
            }
        }
    }

    if (reply->error() == QNetworkReply::NoError && !m_profileStorage)
        setErrorIfFirst(tr("The profile store is unavailable."));
    reply->deleteLater();
    m_currentReply = nullptr;
    downloadNextProfile();
}

void DataMigrationClient::doImportShots()
{
    setCurrentOperation(tr("Importing shot history..."));

    QUrl url(m_serverUrl + "/api/backup/shots");
    QNetworkRequest request(url);
    addSessionCookie(request);

    m_currentReply = m_networkManager->get(request);
    setupSslHandling(m_currentReply);
    connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onShotsReply);
    connect(m_currentReply, &QNetworkReply::downloadProgress, this, &DataMigrationClient::onDownloadProgress);
}

void DataMigrationClient::onShotsReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // Safety check: if cancelled or reply doesn't match (stale signal from race condition)
    if (m_cancelled || reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to import shots:" << reply->errorString();
        setErrorIfFirst(tr("The shot history could not be fetched from the other device."));
        reply->deleteLater();
        m_currentReply = nullptr;
        startNextImport();
        return;
    }

    if (!m_shotHistory) {
        setErrorIfFirst(tr("The shot history store is unavailable."));
        reply->deleteLater();
        m_currentReply = nullptr;
        startNextImport();
        return;
    }

    QByteArray dbData = reply->readAll();
    m_receivedBytes += dbData.size();

    reply->deleteLater();
    m_currentReply = nullptr;

    auto directory = std::make_shared<QTemporaryDir>();
    QObject* context = QCoreApplication::instance();
    if (!directory->isValid() || !context) {
        setErrorIfFirst(tr("The shot history download could not be saved."));
        startNextImport();
        return;
    }
    const QString tempDbPath = directory->filePath(QStringLiteral("shots.db"));
    const QString destDbPath = m_shotHistory->databasePath();
    const int beforeCount = m_shotHistory->totalShots();
    const quint64 generation = m_importGeneration;
    const QPointer<DataMigrationClient> guard(this);
    m_shotImportInFlight = true;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_shotImportCancelled = cancelled;
    // The global pool is joined during application destruction (Qt 6.12,
    // qtbase/src/corelib/kernel/qcoreapplication.cpp:959-968). Workers only
    // capture values; the weak client is inspected on the application thread.
    QThreadPool::globalInstance()->start(QRunnable::create(
        [guard, context, directory, dbData, destDbPath, tempDbPath, beforeCount, generation, cancelled] {
        ShotHistoryStorage::ImportResult shotImport;
        bool saved = false;
        if (!cancelled->load(std::memory_order_relaxed)) {
            QFile file(tempDbPath);
            saved = file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                && file.write(dbData) == dbData.size() && file.flush();
        }
        const bool success = saved && !cancelled->load(std::memory_order_relaxed)
            && ShotHistoryStorage::importDatabaseStatic(
            destDbPath, tempDbPath, true, &shotImport);
        const int afterCount = success ? ShotHistoryStorage::getShotCountStatic(destDbPath) : 0;
        QMetaObject::invokeMethod(context,
            [guard, directory, saved, success, beforeCount, afterCount, generation, shotImport] {
            Q_UNUSED(directory)
            if (!guard) return;
            auto* self = guard.data();
            self->m_shotImportInFlight = false;
            self->m_shotImportCancelled.reset();
            // A started DB transaction cannot be undone by Cancel. Refresh what
            // actually landed, then suppress every retired workflow continuation.
            if (success && self->m_shotHistory) {
                self->m_shotHistory->refreshTotalShots();
                emit self->m_shotHistory->importedDatabaseChanged();
            }
            if (!guard) return;
            emit self->isImportingChanged();
            if (!guard || self->m_importGeneration != generation || self->m_cancelled) return;
            self->m_shotImport = shotImport;
            if (!saved) {
                self->setErrorIfFirst(self->tr("The shot history download could not be saved."));
            } else if (!success) {
                if (!shotImport.integrityFailure.isEmpty())
                    self->setErrorIfFirst(self->tr("Shots were not imported: %1").arg(shotImport.integrityFailure));
                else
                    self->setErrorIfFirst(self->tr("Shots could not be imported. Your existing shots were not changed."));
            } else {
                self->m_shotsImported = afterCount > beforeCount ? afterCount - beforeCount : 0;
            }
            if (!guard || self->m_importGeneration != generation || self->m_cancelled) return;
            self->startNextImport();
        }, Qt::QueuedConnection);
    }));
}

void DataMigrationClient::doImportMedia()
{
    setCurrentOperation(tr("Fetching media list..."));

    // First fetch the list of media files
    QUrl url(m_serverUrl + "/api/backup/media");
    QNetworkRequest request(url);
    addSessionCookie(request);

    m_currentReply = m_networkManager->get(request);
    setupSslHandling(m_currentReply);
    connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onMediaListReply);
}

void DataMigrationClient::onMediaListReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // Safety check: if cancelled or reply doesn't match (stale signal from race condition)
    if (m_cancelled || reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to fetch media list:" << reply->errorString();
        setErrorIfFirst(tr("The media list could not be fetched from the other device."));
        reply->deleteLater();
        m_currentReply = nullptr;
        startNextImport();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    m_currentReply = nullptr;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Invalid media list response";
        setErrorIfFirst(tr("The other device did not return a usable media list."));
        startNextImport();
        return;
    }

    // Build list of media files to download
    m_pendingMedia.clear();
    QJsonArray mediaFiles = doc.array();
    for (const QJsonValue& val : mediaFiles) {
        QJsonObject obj = val.toObject();
        MediaDownload md;
        md.filename = obj["filename"].toString();
        md.size = obj["size"].toInt();
        m_pendingMedia.append(md);
    }

    DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Found" << m_pendingMedia.size() << "media files to download";
    downloadNextMedia();
}

void DataMigrationClient::downloadNextMedia()
{
    if (m_cancelled) {
        m_pendingMedia.clear();
        startNextImport();
        return;
    }

    if (m_pendingMedia.isEmpty()) {
        DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Imported" << m_mediaImported << "media files";
        startNextImport();
        return;
    }

    MediaDownload md = m_pendingMedia.takeFirst();
    setCurrentOperation(tr("Importing media: %1").arg(md.filename));

    // URL encode the filename
    QString encodedFilename = QUrl::toPercentEncoding(md.filename);
    QUrl url(m_serverUrl + "/api/backup/media/" + encodedFilename);
    QNetworkRequest request(url);
    addSessionCookie(request);

    m_currentReply = m_networkManager->get(request);
    setupSslHandling(m_currentReply);
    m_currentReply->setProperty("filename", md.filename);
    connect(m_currentReply, &QNetworkReply::finished, this, &DataMigrationClient::onMediaFileReply);
    connect(m_currentReply, &QNetworkReply::downloadProgress, this, &DataMigrationClient::onDownloadProgress);
}

void DataMigrationClient::onMediaFileReply()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    // Safety check: if cancelled or reply doesn't match (stale signal from race condition)
    if (m_cancelled || reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    QString filename = reply->property("filename").toString();

    if (reply->error() != QNetworkReply::NoError) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to download media" << filename << ":" << reply->errorString();
        setErrorIfFirst(tr("A media file could not be fetched from the other device."));
    } else if (m_screensaver) {
        QByteArray content = reply->readAll();
        m_receivedBytes += content.size();

        // Save to temp file first, then add via manager
        delete m_tempDir;
        m_tempDir = new QTemporaryDir();
        QString tempPath = m_tempDir->path() + "/" + filename;

        QFile outFile(tempPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            const bool written = outFile.write(content) == content.size() && outFile.flush();
            outFile.close();

            // Add to personal media (handles duplicates internally)
            if (!written) {
                setErrorIfFirst(tr("A media file could not be saved."));
            } else if (!m_screensaver->hasPersonalMediaWithName(filename)) {
                if (m_screensaver->addPersonalMedia(tempPath, filename)) m_mediaImported++;
                else setErrorIfFirst(tr("A media file could not be saved."));
            }
        } else {
            setErrorIfFirst(tr("A media file could not be saved."));
        }
    } else {
        setErrorIfFirst(tr("The media store is unavailable."));
    }

    reply->deleteLater();
    m_currentReply = nullptr;
    downloadNextMedia();
}

void DataMigrationClient::cancel()
{
    if (m_fullRestoreInFlight) return;
    ++m_importGeneration;
    m_cancelled = true;
    if (m_shotImportCancelled)
        m_shotImportCancelled->store(true, std::memory_order_relaxed);

    // Tear down any in-flight reachability probe.
    if (m_probeTimer) {
        m_probeTimer->stop();
    }
    teardownProbeSocket();
    m_probeCandidates.clear();
    m_probeIndex = 0;

    if (m_currentReply) {
        // Store pointer locally and clear member FIRST
        // This ensures any queued signal handlers will see reply != m_currentReply
        QNetworkReply* reply = m_currentReply;
        m_currentReply = nullptr;

        // Now disconnect and abort
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_fullBackupDownload.reset();

    if (m_connecting) {
        m_connecting = false;
        emit isConnectingChanged();
    }

    if (m_importing) {
        m_importing = false;
        emit isImportingChanged();
    }

    m_importQueue.clear();
    m_pendingProfiles.clear();
    m_pendingMedia.clear();
    setCurrentOperation(tr("Cancelled"));
}

void DataMigrationClient::onDownloadProgress(qint64 received, qint64 total)
{
    Q_UNUSED(total)

    if (m_totalBytes > 0) {
        double progress = static_cast<double>(m_receivedBytes + received) / m_totalBytes;
        setProgress(qMin(progress, 0.99));  // Cap at 99% until complete
    }
}

void DataMigrationClient::setProgress(double progress)
{
    if (qAbs(m_progress - progress) > 0.001) {
        m_progress = progress;
        emit progressChanged();
    }
}

void DataMigrationClient::setCurrentOperation(const QString& operation)
{
    if (m_currentOperation != operation) {
        m_currentOperation = operation;
        emit currentOperationChanged();
    }
}

void DataMigrationClient::setAiConversationNote(const QString& note)
{
    if (m_aiConversationNote == note) return;
    m_aiConversationNote = note;
    emit aiConversationNoteChanged();
}

void DataMigrationClient::setError(const QString& error)
{
    m_errorMessage = error;
    emit errorMessageChanged();
    DIAG_WARN(STORAGE, "DataMigrationClient") << error;
}

void DataMigrationClient::setErrorIfFirst(const QString& error)
{
    // The dialog shows ONE errorMessage, and the steps run in a fixed order with
    // ai_conversations last (importAll). An unconditional write therefore lets a
    // late, minor failure replace an early, serious one: a refused shot history
    // — the message that names which counts disagreed and says the existing
    // shots are intact — was being overwritten by "the conversations list was
    // not usable", so the user never learned their history had been refused.
    // First failure wins; every failure is still logged by its own call site.
    if (!m_errorMessage.isEmpty()) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "additional failure (not shown, an earlier one "
                      "is already displayed) -" << error;
        return;
    }
    setError(error);
}

// ============================================================================
// Device Discovery
// ============================================================================

void DataMigrationClient::startDiscovery()
{
    if (m_searching) {
        return;
    }
    LocalNetworkAccess::request(LocalNetworkAccess::Feature::DeviceMigration);

    m_searching = true;
    m_discoveredDevices.clear();
    emit isSearchingChanged();
    emit discoveredDevicesChanged();

    setCurrentOperation(tr("Searching for devices..."));

    // Create UDP socket for discovery
    if (m_discoverySocket) {
        m_discoverySocket->close();
        delete m_discoverySocket;
    }
    m_discoverySocket = new QUdpSocket(this);

    // Bind to receive responses (random port)
    if (!m_discoverySocket->bind(QHostAddress::Any, 0)) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to bind discovery socket:" << m_discoverySocket->errorString();
        stopDiscovery();
        return;
    }

    connect(m_discoverySocket, &QUdpSocket::readyRead, this, &DataMigrationClient::onDiscoveryDatagram);

    // Send the first broadcast burst immediately, then retransmit a few times
    // during the discovery window to cover UDP packet loss.
    m_discoveryRetransmitIndex = 0;
    sendDiscoveryBroadcasts();
    m_discoveryRetransmitIndex = 1;

    if (!m_discoveryRetransmitTimer) {
        m_discoveryRetransmitTimer = new QTimer(this);
        m_discoveryRetransmitTimer->setSingleShot(true);
        connect(m_discoveryRetransmitTimer, &QTimer::timeout, this, &DataMigrationClient::onDiscoveryRetransmit);
    }
    constexpr int retransmitCount = sizeof(DISCOVERY_RETRANSMIT_SCHEDULE_MS) / sizeof(DISCOVERY_RETRANSMIT_SCHEDULE_MS[0]);
    if (m_discoveryRetransmitIndex < retransmitCount) {
        m_discoveryRetransmitTimer->start(DISCOVERY_RETRANSMIT_SCHEDULE_MS[m_discoveryRetransmitIndex]);
    }

    // Set up overall timeout timer
    if (!m_discoveryTimer) {
        m_discoveryTimer = new QTimer(this);
        m_discoveryTimer->setSingleShot(true);
        connect(m_discoveryTimer, &QTimer::timeout, this, &DataMigrationClient::onDiscoveryTimeout);
    }
    m_discoveryTimer->start(DISCOVERY_TIMEOUT_MS);
}

void DataMigrationClient::sendDiscoveryBroadcasts()
{
    if (!m_discoverySocket) {
        return;
    }

    QByteArray discoveryMessage = "DECENZA_DISCOVER";
    const int burst = m_discoveryRetransmitIndex;

    // Send to global broadcast address
    qint64 sent = m_discoverySocket->writeDatagram(discoveryMessage, QHostAddress::Broadcast, DISCOVERY_PORT);
    if (sent == -1) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to send broadcast (burst" << burst << "):" << m_discoverySocket->errorString();
        DIAG_WARN(STORAGE, "DataMigrationClient") << "This may be due to firewall, network configuration, or missing permissions";
    } else {
        DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Sent discovery broadcast to 255.255.255.255"
                 << "port" << DISCOVERY_PORT << "(burst" << burst << "," << sent << "bytes)";
    }

    // Also send to directed subnet broadcast addresses for every up interface —
    // 255.255.255.255 is filtered on many networks, but 192.168.x.255 usually gets through.
    int interfaceCount = 0;
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp) ||
            !(interface.flags() & QNetworkInterface::IsRunning) ||
            (interface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        interfaceCount++;
        if (burst == 0) {
            DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Interface" << interface.name() << "is up";
        }

        for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                QHostAddress broadcast = entry.broadcast();
                if (burst == 0) {
                    DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Local IP:" << entry.ip().toString()
                             << "Broadcast:" << (broadcast.isNull() ? "none" : broadcast.toString());
                }
                if (!broadcast.isNull() && broadcast != QHostAddress::Broadcast) {
                    const qint64 bytesSent = m_discoverySocket->writeDatagram(discoveryMessage, broadcast, DISCOVERY_PORT);
                    if (bytesSent > 0) {
                        DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Sent discovery to" << broadcast.toString()
                                 << "(burst" << burst << "," << bytesSent << "bytes)";
                    } else {
                        DIAG_WARN(STORAGE, "DataMigrationClient") << "Failed to send to" << broadcast.toString()
                                   << "(burst" << burst << "):" << m_discoverySocket->errorString();
                    }
                }
            }
        }
    }
    if (interfaceCount == 0 && burst == 0) {
        DIAG_WARN(STORAGE, "DataMigrationClient") << "No active network interfaces found!";
    }
}

void DataMigrationClient::onDiscoveryRetransmit()
{
    if (!m_searching) {
        return;
    }

    sendDiscoveryBroadcasts();
    m_discoveryRetransmitIndex++;

    constexpr int retransmitCount = sizeof(DISCOVERY_RETRANSMIT_SCHEDULE_MS) / sizeof(DISCOVERY_RETRANSMIT_SCHEDULE_MS[0]);
    if (m_discoveryRetransmitIndex < retransmitCount) {
        const int delay = DISCOVERY_RETRANSMIT_SCHEDULE_MS[m_discoveryRetransmitIndex]
                          - DISCOVERY_RETRANSMIT_SCHEDULE_MS[m_discoveryRetransmitIndex - 1];
        m_discoveryRetransmitTimer->start(delay);
    }
}

void DataMigrationClient::stopDiscovery()
{
    if (m_discoveryTimer) {
        m_discoveryTimer->stop();
    }

    if (m_discoveryRetransmitTimer) {
        m_discoveryRetransmitTimer->stop();
    }

    if (m_discoverySocket) {
        m_discoverySocket->close();
        delete m_discoverySocket;
        m_discoverySocket = nullptr;
    }

    if (m_searching) {
        m_searching = false;
        emit isSearchingChanged();
        setCurrentOperation(m_discoveredDevices.isEmpty() ? tr("No devices found") : tr("Search complete"));
    }
}

void DataMigrationClient::onDiscoveryDatagram()
{
    while (m_discoverySocket && m_discoverySocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_discoverySocket->pendingDatagramSize());
        QHostAddress senderAddress;

        m_discoverySocket->readDatagram(datagram.data(), datagram.size(), &senderAddress);

        // Parse response JSON
        QJsonDocument doc = QJsonDocument::fromJson(datagram);
        if (!doc.isObject()) {
            continue;
        }

        QJsonObject obj = doc.object();
        if (obj["type"].toString() != "DECENZA_SERVER") {
            continue;
        }

        // Filter out our own device by checking if sender IP is one of our local IPs
        QString senderIp = senderAddress.toString();
        // Remove IPv6 prefix if present (e.g., "::ffff:192.168.1.100" -> "192.168.1.100")
        if (senderIp.startsWith("::ffff:")) {
            senderIp = senderIp.mid(7);
        }
        bool isOurself = false;
        for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
            for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
                if (entry.ip().toString() == senderIp) {
                    isOurself = true;
                    break;
                }
            }
            if (isOurself) break;
        }
        if (isOurself) {
            DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Ignoring own device at" << senderIp;
            continue;
        }

        // Check if we already have this server (by URL)
        QString serverUrl = obj["serverUrl"].toString();
        bool alreadyFound = false;
        for (const QVariant& device : m_discoveredDevices) {
            QVariantMap deviceMap = device.toMap();
            if (deviceMap["serverUrl"].toString() == serverUrl) {
                alreadyFound = true;
                break;
            }
        }

        if (!alreadyFound) {
            QVariantMap device;
            device["deviceName"] = obj["deviceName"].toString();
            device["platform"] = obj["platform"].toString();
            device["appVersion"] = obj["appVersion"].toString();
            device["serverUrl"] = serverUrl;
            device["port"] = obj["port"].toInt();
            device["ipAddress"] = senderIp;

            m_discoveredDevices.append(device);
            emit discoveredDevicesChanged();

            DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Found device:" << device["deviceName"].toString()
                     << "at" << serverUrl;
        }
    }
}

void DataMigrationClient::onDiscoveryTimeout()
{
    DIAG_DEBUG(STORAGE, "DataMigrationClient") << "Discovery timeout, found" << m_discoveredDevices.size() << "devices";
    stopDiscovery();
    emit discoveryComplete();
}
