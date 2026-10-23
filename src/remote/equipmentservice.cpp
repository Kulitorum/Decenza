#include "equipmentservice.h"
#include <QRemoteObjectNode>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
}

EquipmentService::EquipmentService(QRemoteObjectNode* node, QObject* parent) : RemoteService(parent)
{
    connect(&m_operation, &ServiceOperation::busyChanged, this, &EquipmentService::busyChanged);
    connect(&m_operation, &ServiceOperation::errorChanged, this, &EquipmentService::stateChanged);
    connect(&m_operation, &ServiceOperation::completed, this,
        [this](const QString& uiId, const QString& operation, qint64 id, bool success,
               qint64 resultId, const QString& reason, const QVariantMap&) {
            report(uiId, operation, id, success, resultId, reason);
        });
    setNode(node);
}
EquipmentService::~EquipmentService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}
void EquipmentService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* EquipmentService::publishedNode() { return s_publishedNode; }

bool EquipmentService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}
bool EquipmentService::ready() const { return connected() && m_state->inventoryReady(); }
bool EquipmentService::loading() const { return connected() && m_state->loading(); }
QVariantList EquipmentService::packages() const { return connected() ? m_state->packages() : QVariantList(); }
QVariantMap EquipmentService::options() const { return connected() ? m_state->options() : QVariantMap(); }
QVariantMap EquipmentService::createSeed() const { return connected() ? m_state->createSeed() : QVariantMap(); }
qint64 EquipmentService::activePackageId() const { return connected() ? m_state->activePackageId() : -1; }
QVariantList EquipmentService::puckPrepFlags() const { return options().value(QStringLiteral("puckPrepFlags")).toList(); }
QString EquipmentService::errorMessage() const
{
    if (!connected()) return tr("Equipment is disconnected. Reconnect to refresh its state.");
    return m_state->errorMessage();
}

void EquipmentService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &EquipmentService::changed);
    connect(m_state, &QRemoteObjectReplica::notified, this, &EquipmentService::changed);
    connect(m_state, &EquipmentStateReplica::packagesChanged, this, &EquipmentService::changed);
    connect(m_state, &EquipmentStateReplica::inventoryReadyChanged, this, &EquipmentService::changed);
    connect(m_state, &EquipmentStateReplica::loadingChanged, this, &EquipmentService::changed);
    connect(m_state, &EquipmentStateReplica::errorMessageChanged, this, &EquipmentService::changed);
    connect(m_state, &EquipmentStateReplica::optionsChanged, this, &EquipmentService::stateChanged);
    connect(m_state, &EquipmentStateReplica::createSeedChanged, this, &EquipmentService::stateChanged);
    connect(m_state, &EquipmentStateReplica::activePackageIdChanged, this, &EquipmentService::stateChanged);
    // Queue completions even for the native in-process replica so callers can
    // finish assigning their request IDs before a dialog handles the result.
    connect(m_state, &EquipmentStateReplica::operationCompleted, this,
            [this](const QString& id, bool ok, qint64 resultId, const QString& reason) {
                m_operation.finish(id, ok, resultId, reason);
            }, Qt::QueuedConnection);
    connect(m_state, &EquipmentStateReplica::packageResult, this,
            [this](const QString& requestId, qint64 id, const QVariantMap& package, const QString& reason) {
        if (!m_reads.contains(requestId)) return;
        m_reads.remove(requestId);
        if (reason.isEmpty()) emit packageReady(id, package);
        else emit packageFailed(id, reason);
    }, Qt::QueuedConnection);
    connect(m_state, &EquipmentStateReplica::archivedResult, this,
        [this](const QString& requestId, const QVariantList& packages, const QString& reason) {
            if (!m_archivedReads.remove(requestId)) return;
            if (reason.isEmpty()) emit archivedReady(packages);
            else emit archivedFailed(reason);
        }, Qt::QueuedConnection);
    connect(m_state, &EquipmentStateReplica::grindContextResult, this,
        [this](const QString& requestId, const QVariantMap& result, const QString& reason) {
            const auto it = m_grindReads.find(requestId);
            if (it == m_grindReads.end()) return;
            const QVariantMap query = it.value();
            m_grindReads.erase(it);
            reportGrindContext(query, result, reason);
        }, Qt::QueuedConnection);
    connect(m_state, &EquipmentStateReplica::grindCalibrationChanged,
            this, &EquipmentService::grindCalibrationChanged);
    connect(m_state, &EquipmentStateReplica::packagesInvalidated,
            this, &EquipmentService::packagesChanged);
    changed();
}

void EquipmentService::changed()
{
    if (!connected()) failPending();
    emit stateChanged();
    const auto rows = packages();
    if (rows != m_notifiedPackages) {
        m_notifiedPackages = rows;
        emit packagesChanged();
    }
    if (ready() && !loading()) emit inventoryReady(packages());
    else if (connected() && !m_state->loading() && !m_state->errorMessage().isEmpty())
        emit inventoryFailed(m_state->errorMessage());
}
QString EquipmentService::newRequestId() const { return ServiceOperation::newId(); }
void EquipmentService::refresh()
{
    if (connected()) {
        m_operation.clearError();
        emit stateChanged();
        m_state->refresh();
    }
}
void EquipmentService::requestInventory() { refresh(); }
void EquipmentService::requestPackage(qint64 id)
{
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, id] { emit packageFailed(id, QStringLiteral("disconnected")); }, Qt::QueuedConnection);
        return;
    }
    const auto requestId = ServiceOperation::newId();
    m_reads.insert(requestId, id);
    m_state->requestPackage(requestId, id);
}
void EquipmentService::requestArchived()
{
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this] { emit archivedFailed(QStringLiteral("disconnected")); },
                                  Qt::QueuedConnection);
        return;
    }
    const QString requestId = ServiceOperation::newId();
    m_archivedReads.insert(requestId);
    m_state->requestArchived(requestId);
}

void EquipmentService::requestGrindCalibration(qint64 packageId)
{
    requestGrindContext(QStringLiteral("calibration"), {{QStringLiteral("packageId"), packageId}});
}

void EquipmentService::requestGrindSuggestion(qint64 packageId, const QString& beanBrand,
                                              const QString& beanType, const QString& roastDate,
                                              const QString& profileTitle)
{
    requestGrindContext(QStringLiteral("suggestion"), {
        {QStringLiteral("packageId"), packageId}, {QStringLiteral("beanBrand"), beanBrand},
        {QStringLiteral("beanType"), beanType}, {QStringLiteral("roastDate"), roastDate},
        {QStringLiteral("profileTitle"), profileTitle}});
}

void EquipmentService::requestGrindContext(const QString& kind, const QVariantMap& query)
{
    QVariantMap context = query;
    context.insert(QStringLiteral("kind"), kind);
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, context] {
            reportGrindContext(context, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return;
    }
    const QString requestId = ServiceOperation::newId();
    m_grindReads.insert(requestId, context);
    m_state->requestGrindContext(requestId, kind, query);
}

void EquipmentService::reportGrindContext(const QVariantMap& query, QVariantMap result,
                                          const QString& reason)
{
    if (!reason.isEmpty()) result.insert(QStringLiteral("error"), reason);
    if (query.value(QStringLiteral("kind")).toString() == QLatin1String("calibration")) {
        emit grindCalibrationReady(query.value(QStringLiteral("packageId")).toLongLong(), result);
    } else {
        QVariantMap echo = query;
        echo.remove(QStringLiteral("kind"));
        result.insert(echo);
        emit grindSuggestionReady(result);
    }
}

void EquipmentService::requestSetGrindAnchor(qint64 shotId, const QString& side,
                                             qint64 expectedPackageId)
{
    const QString requestId = newRequestId();
    m_grindWrites.insert(requestId, side);
    send(QStringLiteral("setGrindAnchor"), shotId,
         {{QStringLiteral("side"), side}, {QStringLiteral("expectedPackageId"), expectedPackageId}},
         {}, false, requestId);
}

void EquipmentService::requestClearGrindAnchor(qint64 packageId, const QString& side)
{
    const QString requestId = newRequestId();
    m_grindWrites.insert(requestId, side);
    send(QStringLiteral("clearGrindAnchor"), packageId, {{QStringLiteral("side"), side}},
         {}, false, requestId);
}

void EquipmentService::createPackage(const QVariantMap& fields, bool apply, const QString& requestId)
{ send(QStringLiteral("create"), -1, fields, {}, apply, requestId); }
void EquipmentService::updatePackage(qint64 id, const QVariantMap& fields, const QVariantMap& expected, const QString& requestId)
{ send(QStringLiteral("update"), id, fields, expected, false, requestId); }
void EquipmentService::activatePackage(qint64 id, bool apply, const QString& requestId)
{ send(QStringLiteral("activate"), id, {}, {}, apply, requestId); }
void EquipmentService::requestMarkRemoved(qint64 id)
{ send(QStringLiteral("remove"), id, {}, {}, false, {}); }
void EquipmentService::requestDeletePackage(qint64 id)
{ send(QStringLiteral("delete"), id, {}, {}, false, {}); }
void EquipmentService::requestRestorePackage(qint64 id)
{ send(QStringLiteral("restore"), id, {}, {}, false, {}); }

void EquipmentService::send(const QString& operation, qint64 id, const QVariantMap& fields,
                            const QVariantMap& expected, bool apply, const QString& requestId)
{
    const QString wireId = m_operation.begin(operation, id, requestId, ready());
    if (!wireId.isEmpty()) m_state->execute(wireId, operation, id, fields, expected, apply);
}
void EquipmentService::report(const QString& requestId, const QString& operation, qint64 id,
                              bool success, qint64 resultId, const QString& reason)
{
    if (!success) {
        emit operationFailed(id, operation, reason);
    }
    emit operationCompleted(requestId, success, resultId, reason);
    if (operation == QStringLiteral("delete")) emit packageDeleted(id, success);
    if (operation == QStringLiteral("setGrindAnchor"))
        emit grindAnchorMarked(id, m_grindWrites.take(requestId), success ? QString() : reason);
    if (operation == QStringLiteral("clearGrindAnchor"))
        emit grindAnchorCleared(id, m_grindWrites.take(requestId), success);
}
void EquipmentService::failPending()
{
    m_operation.disconnectPending();
    const auto reads = std::exchange(m_reads, {});
    for (auto id : reads)
        QMetaObject::invokeMethod(this, [this, id] { emit packageFailed(id, QStringLiteral("disconnected")); }, Qt::QueuedConnection);
    const auto archivedReads = std::exchange(m_archivedReads, {});
    for (const auto& requestId : archivedReads) {
        Q_UNUSED(requestId)
        QMetaObject::invokeMethod(this, [this] { emit archivedFailed(QStringLiteral("disconnected")); },
                                  Qt::QueuedConnection);
    }
    const auto grindReads = std::exchange(m_grindReads, {});
    for (const auto& query : grindReads)
        QMetaObject::invokeMethod(this, [this, query] {
            reportGrindContext(query, {}, QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
}
