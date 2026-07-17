#include "datamigrationclient.h"
#include "settings.h"
#include "settingsserializer.h"
#include "profilestorage.h"
#include "../profile/profile.h"
#include "../profile/profilesavehelper.h"
#include "../history/shothistorystorage.h"
#include "../screensaver/screensavervideomanager.h"
#include "../ai/aimanager.h"

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
#include <QThread>

DataMigrationClient::DataMigrationClient(QNetworkAccessManager* networkManager, QObject* parent)
    : QObject(parent)
    , m_networkManager(networkManager)
{
    Q_ASSERT(networkManager);
}

DataMigrationClient::~DataMigrationClient()
{
    *m_destroyed = true;
    stopDiscovery();
    cancel();
    delete m_tempDir;
}

bool DataMigrationClient::isTransientNetworkError(QNetworkReply::NetworkError error)
{
    // Errors that mean "the connection could not be established" and are worth a
    // retry / an "unreachable" classification. EHOSTUNREACH surfaces as
    // UnknownNetworkError. Deliberately excludes ConnectionRefusedError and
    // RemoteHostClosedError — those mean the host answered, so it is reachable.
    switch (error) {
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        return true;
    default:
        return false;
    }
}

void DataMigrationClient::setupSslHandling(QNetworkReply* reply)
{
    // Ignore SSL errors for self-signed certificates on LAN migration servers
    connect(reply, &QNetworkReply::sslErrors, this, [reply](const QList<QSslError>& errors) {
        qDebug() << "DataMigrationClient: Ignoring SSL errors for LAN migration:" << errors.size();
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
    QSettings settings;
    settings.beginGroup("migration_sessions");
    settings.setValue(serverHost, token);
    settings.endGroup();
}

QString DataMigrationClient::loadSessionToken(const QString& serverHost)
{
    QSettings settings;
    settings.beginGroup("migration_sessions");
    QString token = settings.value(serverHost).toString();
    settings.endGroup();
    return token;
}

void DataMigrationClient::connectToServer(const QString& serverUrl)
{
    if (m_connecting || m_importing) {
        return;
    }

    // Normalize URL
    m_serverUrl = serverUrl;
    if (!m_serverUrl.startsWith("http://") && !m_serverUrl.startsWith("https://")) {
        m_serverUrl = "http://" + m_serverUrl;
    }
    if (m_serverUrl.endsWith("/")) {
        m_serverUrl.chop(1);
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
    // address has no ambiguity — keep the existing direct path, adding no delay.
    QList<QHostAddress> candidates;
    if (!target.isNull() && target.protocol() == QAbstractSocket::IPv4Protocol) {
        candidates = gatherSubnetCandidates(target, m_connectPort);
    }

    if (candidates.size() < 2) {
        fetchManifest();
        return;
    }

    qDebug() << "DataMigrationClient: multi-homed on target subnet, probing"
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
        qWarning() << "DataMigrationClient: could not bind probe to" << source.toString()
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
    qDebug() << "DataMigrationClient: reachable via local source" << via.toString();
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
    qDebug() << "DataMigrationClient: candidate source" << tried.toString()
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

    // Retry transient network failures (a cold neighbour on a multi-homed subnet
    // can drop the first attempt) before declaring failure. The preflight has
    // already selected a reachable link, so a retry usually succeeds. HTTP status
    // codes and parse errors mean the connection itself worked — never retried.
    if (statusCode != 401 && err != QNetworkReply::NoError &&
        isTransientNetworkError(err) && m_manifestRetriesLeft > 0) {
        m_manifestRetriesLeft--;
        const QString transientMsg = reply->errorString();
        reply->deleteLater();
        m_currentReply = nullptr;
        qDebug() << "DataMigrationClient: transient manifest error (" << transientMsg
                 << ") — retrying," << m_manifestRetriesLeft << "left";
        fetchManifest();
        return;
    }

    m_connecting = false;
    emit isConnectingChanged();

    // Check for 401 (authentication required) — reachable, needs a code.
    if (statusCode == 401) {
        qDebug() << "DataMigrationClient: Server requires authentication (401)";
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
        // the connection dropped/errored" so the message is truthful.
        const QString msg = isTransientNetworkError(err)
            ? tr("Connection failed: device is not reachable on your local network")
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

    qDebug() << "DataMigrationClient: Connected to" << m_serverUrl
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

            qDebug() << "DataMigrationClient: Authenticated successfully, session cached";
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
    if (m_importing || m_serverUrl.isEmpty()) {
        return;
    }

    m_importing = true;
    m_cancelled = false;
    m_settingsImported = 0;
    m_profilesImported = 0;
    m_shotsImported = 0;
    m_mediaImported = 0;
    m_aiConversationsImported = 0;
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

void DataMigrationClient::importAll()
{
    // Build list of all available data types
    QStringList types;
    if (m_manifest["hasSettings"].toBool()) {
        types << "settings";
    }
    if (m_manifest["profileCount"].toInt() > 0) {
        types << "profiles";
    }
    if (m_manifest["shotCount"].toInt() > 0) {
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
    if (m_manifest["shotCount"].toInt() > 0) {
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
        qWarning() << "DataMigrationClient: Failed to import AI conversations:" << reply->errorString();
    } else {
        QByteArray data = reply->readAll();
        m_receivedBytes += data.size();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isArray()) {
            QSettings settings;
            QJsonArray conversations = doc.array();

            // Load existing index to know which keys to skip
            QJsonArray existingIndex;
            QByteArray existingIndexData = settings.value("ai/conversations/index").toByteArray();
            if (!existingIndexData.isEmpty()) {
                QJsonDocument indexDoc = QJsonDocument::fromJson(existingIndexData);
                if (indexDoc.isArray()) existingIndex = indexDoc.array();
            }
            QSet<QString> existingKeys;
            for (const QJsonValue& v : existingIndex) {
                existingKeys.insert(v.toObject()["key"].toString());
            }

            for (const QJsonValue& val : conversations) {
                QJsonObject conv = val.toObject();
                QString key = conv["key"].toString();
                if (key.isEmpty() || existingKeys.contains(key)) continue;

                // Write conversation data to QSettings
                QString prefix = "ai/conversations/" + key + "/";
                settings.setValue(prefix + "systemPrompt", conv["systemPrompt"].toString());
                settings.setValue(prefix + "messages",
                    QJsonDocument(conv["messages"].toArray()).toJson(QJsonDocument::Compact));
                settings.setValue(prefix + "timestamp", conv["timestamp"].toString());
                settings.setValue(prefix + "contextLabel", conv["contextLabel"].toString());

                // Add to index
                QJsonObject indexEntry;
                indexEntry["key"] = key;
                indexEntry["beanBrand"] = conv["beanBrand"].toString();
                indexEntry["beanType"] = conv["beanType"].toString();
                indexEntry["profileName"] = conv["profileName"].toString();
                indexEntry["timestamp"] = conv["indexTimestamp"].toVariant().toLongLong();
                existingIndex.append(indexEntry);
                existingKeys.insert(key);

                m_aiConversationsImported++;
            }

            // Save updated index and reload
            if (m_aiConversationsImported > 0) {
                settings.setValue("ai/conversations/index",
                    QJsonDocument(existingIndex).toJson(QJsonDocument::Compact));

                if (m_aiManager)
                    m_aiManager->reloadConversations();
            }

            qDebug() << "DataMigrationClient: Imported" << m_aiConversationsImported << "AI conversations";
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
        // All done
        m_importing = false;
        setProgress(1.0);
        setCurrentOperation(tr("Import complete"));
        emit isImportingChanged();
        emit importComplete(m_settingsImported, m_profilesImported, m_shotsImported, m_mediaImported, m_aiConversationsImported);
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
        qWarning() << "DataMigrationClient: Failed to import settings:" << reply->errorString();
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
            SettingsSerializer::importFromJson(m_settings, doc.object(), {"flowCalibration", "mqttPassword"});
            m_settingsImported = 1;
            qDebug() << "DataMigrationClient: Settings imported successfully";
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
        // Older server without the endpoint (404) or a transient error — the
        // extra settings simply don't transfer; continue with the import.
        qDebug() << "DataMigrationClient: extra-settings not available:" << reply->errorString();
    } else {
        const QByteArray data = reply->readAll();
        m_receivedBytes += data.size();
        const QJsonObject extra = QJsonDocument::fromJson(data).object();
        if (!extra.isEmpty()) {
            // Apply to the LOCAL stores, mirroring handleBackupRestore's
            // extra_settings.json path.
            QSettings settings;
            if (extra.contains("shotMap")) {
                const QJsonObject sm = extra["shotMap"].toObject();
                settings.setValue("shotMap/manualCity", sm["manualCity"].toString());
                settings.setValue("shotMap/manualLat", sm["manualLat"].toDouble());
                settings.setValue("shotMap/manualLon", sm["manualLon"].toDouble());
                settings.setValue("shotMap/manualCountryCode", sm["manualCountryCode"].toString());
                settings.setValue("shotMap/manualGeocoded", sm["manualGeocoded"].toBool());
            }
            if (extra.contains("accessibility")) {
                // Accessibility lives in the primary DecentEspresso/DE1Qt store.
                QSettings accessStore(QStringLiteral("DecentEspresso"), QStringLiteral("DE1Qt"));
                const QJsonObject a = extra["accessibility"].toObject();
                if (a.contains("enabled")) accessStore.setValue("accessibility/enabled", a["enabled"].toBool());
                if (a.contains("ttsEnabled")) accessStore.setValue("accessibility/ttsEnabled", a["ttsEnabled"].toBool());
                if (a.contains("tickEnabled")) accessStore.setValue("accessibility/tickEnabled", a["tickEnabled"].toBool());
                if (a.contains("tickSoundIndex")) accessStore.setValue("accessibility/tickSoundIndex", a["tickSoundIndex"].toInt());
                if (a.contains("tickVolume")) accessStore.setValue("accessibility/tickVolume", a["tickVolume"].toInt());
                if (a.contains("extractionAnnouncementsEnabled")) accessStore.setValue("accessibility/extractionAnnouncementsEnabled", a["extractionAnnouncementsEnabled"].toBool());
                if (a.contains("extractionAnnouncementInterval")) accessStore.setValue("accessibility/extractionAnnouncementInterval", a["extractionAnnouncementInterval"].toInt());
                if (a.contains("extractionAnnouncementMode")) accessStore.setValue("accessibility/extractionAnnouncementMode", a["extractionAnnouncementMode"].toString());
            }
            if (extra.contains("language"))
                settings.setValue("localization/language", extra["language"].toString());
            qDebug() << "DataMigrationClient: extra settings imported (location, accessibility, language)";
        }
    }

    reply->deleteLater();
    m_currentReply = nullptr;
    startNextImport();
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
        qWarning() << "DataMigrationClient: Failed to fetch profile list:" << reply->errorString();
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
        qWarning() << "DataMigrationClient: Invalid profile list response";
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

    qDebug() << "DataMigrationClient: Found" << m_pendingProfiles.size() << "profiles to download";
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
        qDebug() << "DataMigrationClient: Imported" << m_profilesImported << "profiles";
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
        qWarning() << "DataMigrationClient: Failed to download profile" << filename << ":" << reply->errorString();
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
            qWarning() << "DataMigrationClient: Invalid profile, skipping:" << filename;
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
                qDebug() << "DataMigrationClient: Skipping profile identical to built-in:" << filename;
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
                qDebug() << "DataMigrationClient: Skipping duplicate profile:" << filename;
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
            if (incomingProfile.saveToFile(targetPath)) {
                m_profilesImported++;
                qDebug() << "DataMigrationClient: Imported profile:" << incomingProfile.title();
            } else {
                qWarning() << "DataMigrationClient: Failed to save profile:" << targetPath;
            }
        }
    }

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
        qWarning() << "DataMigrationClient: Failed to import shots:" << reply->errorString();
        reply->deleteLater();
        m_currentReply = nullptr;
        startNextImport();
        return;
    }

    if (!m_shotHistory) {
        reply->deleteLater();
        m_currentReply = nullptr;
        startNextImport();
        return;
    }

    QByteArray dbData = reply->readAll();
    m_receivedBytes += dbData.size();

    reply->deleteLater();
    m_currentReply = nullptr;

    // Save to standalone temp file (not m_tempDir) so the background thread
    // can safely read it even if DataMigrationClient is destroyed mid-import.
    QString tempDbPath = QDir::temp().filePath("decenza_migration_shots.db");
    QFile tempFile(tempDbPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        qWarning() << "DataMigrationClient: Failed to create temp file for shots import:" << tempDbPath;
        startNextImport();
        return;
    }
    tempFile.write(dbData);
    tempFile.close();

    // Import on background thread to avoid UI freeze
    QString destDbPath = m_shotHistory->databasePath();
    int beforeCount = m_shotHistory->totalShots();
    auto destroyed = m_destroyed;

    QThread* thread = QThread::create([this, destDbPath, tempDbPath, beforeCount, destroyed]() {
        bool success = ShotHistoryStorage::importDatabaseStatic(destDbPath, tempDbPath, true);

        // Count shots on background thread right after import (no signal race)
        int afterCount = success ? ShotHistoryStorage::getShotCountStatic(destDbPath) : 0;

        // Clean up temp file on background thread (safe even if object is destroyed)
        QFile::remove(tempDbPath);

        QMetaObject::invokeMethod(this, [this, success, beforeCount, afterCount, destroyed]() {
            if (*destroyed) {
                qDebug() << "DataMigrationClient: Shots import callback dropped (object destroyed)";
                return;
            }

            if (success && m_shotHistory) {
                m_shotsImported = afterCount > beforeCount ? afterCount - beforeCount : 0;
                qDebug() << "DataMigrationClient: Imported" << m_shotsImported << "new shots";
                m_shotHistory->refreshTotalShots();
            }

            startNextImport();
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
        qWarning() << "DataMigrationClient: Failed to fetch media list:" << reply->errorString();
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
        qWarning() << "DataMigrationClient: Invalid media list response";
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

    qDebug() << "DataMigrationClient: Found" << m_pendingMedia.size() << "media files to download";
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
        qDebug() << "DataMigrationClient: Imported" << m_mediaImported << "media files";
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
        qWarning() << "DataMigrationClient: Failed to download media" << filename << ":" << reply->errorString();
    } else if (m_screensaver) {
        QByteArray content = reply->readAll();
        m_receivedBytes += content.size();

        // Save to temp file first, then add via manager
        delete m_tempDir;
        m_tempDir = new QTemporaryDir();
        QString tempPath = m_tempDir->path() + "/" + filename;

        QFile outFile(tempPath);
        if (outFile.open(QIODevice::WriteOnly)) {
            outFile.write(content);
            outFile.close();

            // Add to personal media (handles duplicates internally)
            if (m_screensaver->addPersonalMedia(tempPath, filename)) {
                m_mediaImported++;
            }
        }
    }

    reply->deleteLater();
    m_currentReply = nullptr;
    downloadNextMedia();
}

void DataMigrationClient::cancel()
{
    m_cancelled = true;

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

void DataMigrationClient::setError(const QString& error)
{
    m_errorMessage = error;
    emit errorMessageChanged();
    qWarning() << "DataMigrationClient:" << error;
}

// ============================================================================
// Device Discovery
// ============================================================================

void DataMigrationClient::startDiscovery()
{
    if (m_searching) {
        return;
    }

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
        qWarning() << "DataMigrationClient: Failed to bind discovery socket:" << m_discoverySocket->errorString();
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
        qWarning() << "DataMigrationClient: Failed to send broadcast (burst" << burst << "):" << m_discoverySocket->errorString();
        qWarning() << "DataMigrationClient: This may be due to firewall, network configuration, or missing permissions";
    } else {
        qDebug() << "DataMigrationClient: Sent discovery broadcast to 255.255.255.255"
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
            qDebug() << "DataMigrationClient: Interface" << interface.name() << "is up";
        }

        for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                QHostAddress broadcast = entry.broadcast();
                if (burst == 0) {
                    qDebug() << "DataMigrationClient:   Local IP:" << entry.ip().toString()
                             << "Broadcast:" << (broadcast.isNull() ? "none" : broadcast.toString());
                }
                if (!broadcast.isNull() && broadcast != QHostAddress::Broadcast) {
                    qint64 sent = m_discoverySocket->writeDatagram(discoveryMessage, broadcast, DISCOVERY_PORT);
                    if (sent > 0) {
                        qDebug() << "DataMigrationClient:   Sent discovery to" << broadcast.toString()
                                 << "(burst" << burst << "," << sent << "bytes)";
                    } else {
                        qWarning() << "DataMigrationClient:   Failed to send to" << broadcast.toString()
                                   << "(burst" << burst << "):" << m_discoverySocket->errorString();
                    }
                }
            }
        }
    }
    if (interfaceCount == 0 && burst == 0) {
        qWarning() << "DataMigrationClient: No active network interfaces found!";
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
            qDebug() << "DataMigrationClient: Ignoring own device at" << senderIp;
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

            qDebug() << "DataMigrationClient: Found device:" << device["deviceName"].toString()
                     << "at" << serverUrl;
        }
    }
}

void DataMigrationClient::onDiscoveryTimeout()
{
    qDebug() << "DataMigrationClient: Discovery timeout, found" << m_discoveredDevices.size() << "devices";
    stopDiscovery();
    emit discoveryComplete();
}
