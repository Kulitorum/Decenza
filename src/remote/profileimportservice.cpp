#include "profileimportservice.h"

#include <QMetaObject>
#include <QRemoteObjectNode>
#include <QUuid>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
}

ProfileImportService::ProfileImportService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    setNode(node);
}

ProfileImportService::~ProfileImportService()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

void ProfileImportService::setPublishedNode(QRemoteObjectNode* node)
{
    s_publishedNode = node;
}

QRemoteObjectNode* ProfileImportService::publishedNode()
{
    return s_publishedNode;
}

bool ProfileImportService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

bool ProfileImportService::ready() const
{
    return connected() && m_state->ready();
}

QVariantMap ProfileImportService::snapshot() const
{
    if (!connected())
        return {};
    QVariantMap result = m_state->status();
    result.insert(QStringLiteral("availableProfiles"), m_state->availableProfiles());
    return result;
}

void ProfileImportService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(); }))
        return;

    connect(m_state, &QRemoteObjectReplica::stateChanged,
            this, &ProfileImportService::changed);
    connect(m_state, &QRemoteObjectReplica::notified,
            this, &ProfileImportService::changed);
    connect(m_state, &ProfileImportStateReplica::readyChanged,
            this, &ProfileImportService::changed);
    connect(m_state, &ProfileImportStateReplica::statusChanged,
            this, &ProfileImportService::changed);
    connect(m_state, &ProfileImportStateReplica::availableProfilesChanged,
            this, &ProfileImportService::changed);
    connect(m_state, &ProfileImportStateReplica::operationCompleted, this,
            [this](const QString& id, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason) {
        const auto it = m_pending.find(id);
        if (it == m_pending.end() || it.value() != operation)
            return;
        m_pending.erase(it);
        if (m_duplicateRequestId == id)
            m_duplicateRequestId.clear();
        emit operationCompleted(id, operation, success, result, reason);
        if (!success)
            emit importFailed(reason);
        else if (operation.startsWith(QLatin1String("import"))
                 && result.contains(QLatin1String("title")))
            emit importSuccess(result.value(QLatin1String("title")).toString());
        changed();
    }, Qt::QueuedConnection);
    connect(m_state, &ProfileImportStateReplica::duplicateFound, this,
            [this](const QString& id, const QString& title) {
        if (!m_pending.contains(id))
            return;
        m_duplicateRequestId = id;
        emit duplicateFound(title, QString());
    }, Qt::QueuedConnection);
    connect(m_state, &ProfileImportStateReplica::requestRejected, this,
            [this](const QString& id, const QString& operation, const QString& reason) {
        if (!m_pending.contains(id))
            return;
        emit operationRejected(id, operation, reason);
    }, Qt::QueuedConnection);
    changed();
}

void ProfileImportService::changed()
{
    emit stateChanged();
    emit readyChanged();
}

void ProfileImportService::failPending()
{
    const auto pending = std::exchange(m_pending, {});
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString id = it.key();
        const QString operation = it.value();
        QMetaObject::invokeMethod(this, [this, id, operation] {
            emit operationCompleted(id, operation, false, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
    }
}

QString ProfileImportService::newRequestId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString ProfileImportService::execute(const QString& operation, const QVariantMap& arguments)
{
    if (!connected())
        return {};

    const bool resolvingDuplicate = operation == QLatin1String("saveOverwrite")
        || operation == QLatin1String("saveAsNew")
        || operation == QLatin1String("saveWithNewName")
        || operation == QLatin1String("cancelImport");
    QString id;
    if (resolvingDuplicate) {
        id = m_duplicateRequestId;
        if (id.isEmpty())
            return {};
    } else {
        id = newRequestId();
        m_pending.insert(id, operation);
    }
    m_state->execute(id, operation, arguments);
    return id;
}

void ProfileImportService::refresh()
{
    if (connected())
        m_state->refresh();
}

void ProfileImportService::scanProfiles() { execute(QStringLiteral("scanProfiles")); }
void ProfileImportService::scanTransfer(const QString& id)
{
    execute(QStringLiteral("scanTransfer"), {{QStringLiteral("transferId"), id}});
}
void ProfileImportService::importTransfer(const QString& id)
{
    execute(QStringLiteral("importTransfer"), {{QStringLiteral("transferId"), id}});
}
void ProfileImportService::importById(const QString& id, bool force)
{
    execute(force ? QStringLiteral("forceImport") : QStringLiteral("importProfile"),
            {{QStringLiteral("profileId"), id}});
}
void ProfileImportService::importAllNew() { execute(QStringLiteral("importAllNew")); }
void ProfileImportService::updateAllDifferent() { execute(QStringLiteral("updateAllDifferent")); }
void ProfileImportService::saveOverwrite() { execute(QStringLiteral("saveOverwrite")); }
void ProfileImportService::saveAsNew() { execute(QStringLiteral("saveAsNew")); }
void ProfileImportService::saveWithNewName(const QString& name)
{
    execute(QStringLiteral("saveWithNewName"), {{QStringLiteral("name"), name}});
}
void ProfileImportService::cancelImport() { execute(QStringLiteral("cancelImport")); }
