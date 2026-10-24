#include "datamigrationservicesource.h"
#include "datamigrationcommandpolicy.h"
#include "serviceoperation.h"
#include "core/datamigrationclient.h"

NativeDataMigrationServiceSource::NativeDataMigrationServiceSource(
    DataMigrationClient* client, QObject* parent)
    : DataMigrationStateSimpleSource(parent), m_client(client)
{
    if (m_client) {
        connect(m_client, &QObject::destroyed, this, [this] {
            failActive(QStringLiteral("unavailable"));
            m_client = nullptr;
            refreshSnapshot();
        });
        connect(m_client, &DataMigrationClient::discoveryComplete, this, [this] {
            refreshSnapshot();
            if (m_activeOperation == QLatin1String("startDiscovery"))
                completeActive(true);
        });
        connect(m_client, &DataMigrationClient::connected, this, [this] {
            refreshSnapshot();
            if (m_activeOperation == QLatin1String("connectToServer"))
                completeActive(true, {{QStringLiteral("needsAuthentication"), false}});
        });
        connect(m_client, &DataMigrationClient::connectionFailed, this, [this](const QString& error) {
            refreshSnapshot();
            if (m_activeOperation == QLatin1String("connectToServer"))
                completeActive(false, {}, error);
        });
        connect(m_client, &DataMigrationClient::needsAuthenticationChanged, this, [this] {
            refreshSnapshot();
            if (m_client && m_client->needsAuthentication()
                && m_activeOperation == QLatin1String("connectToServer"))
                completeActive(true, {{QStringLiteral("needsAuthentication"), true}});
        });
        connect(m_client, &DataMigrationClient::authenticationSucceeded, this, [this] {
            refreshSnapshot();
            if (m_activeOperation == QLatin1String("authenticate"))
                completeActive(true);
        });
        connect(m_client, &DataMigrationClient::authenticationFailed, this, [this](const QString& error) {
            refreshSnapshot();
            if (m_activeOperation == QLatin1String("authenticate"))
                completeActive(false, {}, error);
        });
        connect(m_client, &DataMigrationClient::importComplete, this,
                [this](int settings, int profiles, int shots, int media, int conversations) {
            const QVariantMap result{
                {QStringLiteral("settingsImported"), settings},
                {QStringLiteral("profilesImported"), profiles},
                {QStringLiteral("shotsImported"), shots},
                {QStringLiteral("mediaImported"), media},
                {QStringLiteral("aiConversationsImported"), conversations},
                {QStringLiteral("aiConversationNote"), m_client ? m_client->aiConversationNote() : QString()}};
            refreshSnapshot();
            if (m_activeOperation.startsWith(QLatin1String("import")))
                completeActive(true, result);
        });
        connect(m_client, &DataMigrationClient::importFailed, this, [this](const QString& error) {
            const QVariantMap result{{QStringLiteral("aiConversationNote"),
                                      m_client ? m_client->aiConversationNote() : QString()}};
            refreshSnapshot();
            if (m_activeOperation.startsWith(QLatin1String("import")))
                completeActive(false, result, error);
        });
        connect(m_client, &DataMigrationClient::progressChanged, this, [this] {
            refreshSnapshot();
            if (!m_activeRequestId.isEmpty() && m_activeOperation.startsWith(QLatin1String("import")))
                emit operationProgress(m_activeRequestId, m_client ? m_client->progress() : 0.0,
                                       m_client ? m_client->currentOperation() : QString());
        });
        connect(m_client, &DataMigrationClient::isConnectingChanged, this, &NativeDataMigrationServiceSource::refreshSnapshot);
        connect(m_client, &DataMigrationClient::isImportingChanged, this, &NativeDataMigrationServiceSource::refreshSnapshot);
        connect(m_client, &DataMigrationClient::isSearchingChanged, this, [this] {
            refreshSnapshot();
            if (m_client && !m_client->isSearching()
                && m_activeOperation == QLatin1String("startDiscovery"))
                completeActive(true);
        });
        connect(m_client, &DataMigrationClient::currentOperationChanged, this, &NativeDataMigrationServiceSource::refreshSnapshot);
        connect(m_client, &DataMigrationClient::errorMessageChanged, this, &NativeDataMigrationServiceSource::refreshSnapshot);
        connect(m_client, &DataMigrationClient::aiConversationNoteChanged, this, &NativeDataMigrationServiceSource::refreshSnapshot);
        connect(m_client, &DataMigrationClient::manifestChanged, this, &NativeDataMigrationServiceSource::refreshSnapshot);
        connect(m_client, &DataMigrationClient::serverUrlChanged, this, &NativeDataMigrationServiceSource::refreshSnapshot);
        connect(m_client, &DataMigrationClient::discoveredDevicesChanged, this, &NativeDataMigrationServiceSource::refreshSnapshot);
    }
    refreshSnapshot();
}

QVariantMap NativeDataMigrationServiceSource::makeSnapshot() const
{
    if (!m_client) return {};
    return {{QStringLiteral("isConnecting"), m_client->isConnecting()},
            {QStringLiteral("isImporting"), m_client->isImporting()},
            {QStringLiteral("isSearching"), m_client->isSearching()},
            {QStringLiteral("progress"), qBound(0.0, m_client->progress(), 1.0)},
            {QStringLiteral("currentOperation"), m_client->currentOperation().left(256)},
            {QStringLiteral("errorMessage"), m_client->errorMessage().left(1024)},
            {QStringLiteral("aiConversationNote"), m_client->aiConversationNote().left(2048)},
            {QStringLiteral("manifest"), DataMigrationCommandPolicy::safeManifest(m_client->manifest())},
            {QStringLiteral("serverUrl"), m_client->serverUrl().left(512)},
            {QStringLiteral("discoveredDevices"), DataMigrationCommandPolicy::safeDevices(m_client->discoveredDevices())},
            {QStringLiteral("needsAuthentication"), m_client->needsAuthentication()}};
}

void NativeDataMigrationServiceSource::refreshSnapshot()
{
    m_ready = m_client;
    setSnapshot(makeSnapshot());
    setReady(m_ready);
}

void NativeDataMigrationServiceSource::refresh() { refreshSnapshot(); }

void NativeDataMigrationServiceSource::completeActive(bool success, const QVariantMap& result,
                                                       const QString& reason)
{
    if (m_activeRequestId.isEmpty()) return;
    const QString id = m_activeRequestId;
    const QString operation = m_activeOperation;
    m_activeRequestId.clear();
    m_activeOperation.clear();
    refreshSnapshot();
    complete(id, operation, success, result, reason);
}

void NativeDataMigrationServiceSource::complete(const QString& requestId, const QString& operation,
                                                 bool success, const QVariantMap& result,
                                                 const QString& reason)
{
    m_requests.finish(requestId);
    emit operationCompleted(requestId, operation, success, result, reason);
}

void NativeDataMigrationServiceSource::failActive(const QString& reason)
{
    completeActive(false, {}, reason);
}

void NativeDataMigrationServiceSource::execute(QString requestId, QString operation,
                                                QVariantMap arguments)
{
    const ServiceRequestIds::Start started = m_requests.begin(requestId);
    if (started == ServiceRequestIds::Start::DuplicateId)
        return;
    if (started != ServiceRequestIds::Start::Accepted) {
        QString why = QStringLiteral("invalidRequest");
        if (started == ServiceRequestIds::Start::LimitReached) why = QStringLiteral("tooManyRequests");
        emit operationCompleted(requestId, operation, false, {}, why);
        return;
    }
    if (!m_client) {
        complete(requestId, operation, false, {}, QStringLiteral("unavailable"));
        return;
    }
    QString reason;
    if (!DataMigrationCommandPolicy::validate(operation, arguments, &reason)) {
        complete(requestId, operation, false, {}, reason);
        return;
    }

    const bool interrupt = operation == QLatin1String("cancel")
        || operation == QLatin1String("disconnect")
        || (operation == QLatin1String("stopDiscovery") && m_activeOperation == QLatin1String("startDiscovery"));
    if (!interrupt && (!m_activeRequestId.isEmpty() || m_client->isConnecting() || m_client->isImporting())) {
        complete(requestId, operation, false, {}, QStringLiteral("busy"));
        return;
    }

    // Canonical individual import methods intentionally do nothing when the
    // selected data is absent. Refuse that request here so it still has a
    // correlated terminal result at the service boundary.
    const bool import = operation.startsWith(QLatin1String("import"));
    if ((import || operation == QLatin1String("authenticate")) && m_client->serverUrl().isEmpty()) {
        complete(requestId, operation, false, {}, QStringLiteral("notConnected"));
        return;
    }
    if (import) {
        const QVariantMap manifest = m_client->manifest();
        if (manifest.isEmpty() || m_client->isConnecting() || m_client->needsAuthentication()) {
            complete(requestId, operation, false, {}, QStringLiteral("notConnected"));
            return;
        }
        const bool available = operation == QLatin1String("importAll")
            || (operation == QLatin1String("importOnlySettings") && manifest.value(QStringLiteral("hasSettings")).toBool())
            || (operation == QLatin1String("importOnlyProfiles") && manifest.value(QStringLiteral("profileCount")).toInt() > 0)
            || (operation == QLatin1String("importOnlyShots") && manifest.value(QStringLiteral("shotCount")).toInt() > 0)
            || (operation == QLatin1String("importOnlyMedia") && manifest.value(QStringLiteral("mediaCount")).toInt() > 0)
            || (operation == QLatin1String("importOnlyAIConversations") && manifest.value(QStringLiteral("aiConversationCount")).toInt() > 0);
        if (!available) {
            complete(requestId, operation, false, {}, QStringLiteral("dataUnavailable"));
            return;
        }
    }

    if (interrupt && !m_activeRequestId.isEmpty())
        failActive(QStringLiteral("cancelled"));

    m_activeRequestId = requestId;
    m_activeOperation = operation;
    if (operation == QLatin1String("startDiscovery")) {
        m_client->startDiscovery();
    } else if (operation == QLatin1String("stopDiscovery")) {
        m_client->stopDiscovery();
        completeActive(true);
    } else if (operation == QLatin1String("connectToServer")) {
        m_client->connectToServer(arguments.value(QStringLiteral("serverUrl")).toString().trimmed());
    } else if (operation == QLatin1String("authenticate")) {
        const QString code = arguments.take(QStringLiteral("totpCode")).toString();
        m_client->authenticate(code);
    } else if (operation == QLatin1String("disconnect")) {
        m_client->disconnect();
        completeActive(true);
    } else if (operation == QLatin1String("cancel")) {
        m_client->cancel();
        completeActive(true);
    } else if (operation == QLatin1String("importAll")) {
        m_client->importAll();
    } else if (operation == QLatin1String("importOnlySettings")) {
        m_client->importOnlySettings();
    } else if (operation == QLatin1String("importOnlyProfiles")) {
        m_client->importOnlyProfiles();
    } else if (operation == QLatin1String("importOnlyShots")) {
        m_client->importOnlyShots();
    } else if (operation == QLatin1String("importOnlyMedia")) {
        m_client->importOnlyMedia();
    } else if (operation == QLatin1String("importOnlyAIConversations")) {
        m_client->importOnlyAIConversations();
    }
    refreshSnapshot();
}
