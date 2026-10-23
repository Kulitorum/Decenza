#include "bagservice.h"

#include "../core/coffeebagfields.h"

#include <QDate>
#include <QMetaObject>
#include <QRemoteObjectNode>
#include <utility>

namespace {
QPointer<QRemoteObjectNode> s_publishedNode;
}

BagService::BagService(QRemoteObjectNode* node, QObject* parent)
    : RemoteService(parent)
{
    connect(&m_operation, &ServiceOperation::busyChanged, this, &BagService::busyChanged);
    connect(&m_operation, &ServiceOperation::errorChanged, this, &BagService::stateChanged);
    connect(&m_operation, &ServiceOperation::completed, this,
        [this](const QString& uiId, const QString& operation, qint64 bagId,
               bool success, qint64 resultId, const QString& reason, const QVariantMap& result) {
            Q_UNUSED(resultId)
            report(uiId, operation, bagId, success, result, reason);
        });
    setNode(node);
}

BagService::~BagService()
{
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
}

void BagService::setPublishedNode(QRemoteObjectNode* node) { s_publishedNode = node; }
QRemoteObjectNode* BagService::publishedNode() { return s_publishedNode; }

bool BagService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_publishedNode);
}

QVariantList BagService::bags() const { return connected() ? m_state->inventory() : QVariantList(); }
QVariantList BagService::finishedBags() const { return connected() ? m_state->finishedBags() : QVariantList(); }
qint64 BagService::activeBagId() const { return connected() ? m_state->activeBagId() : -1; }
bool BagService::ready() const { return connected() && m_state->inventoryReady(); }
bool BagService::loading() const { return connected() && m_state->loading(); }

QString BagService::errorMessage() const
{
    if (!connected())
        return tr("Bag storage is disconnected. Reconnect to refresh its state.");
    return m_state->errorMessage();
}

void BagService::setNode(QRemoteObjectNode* node)
{
    if (m_node == node && m_state && (node || !m_nodeDestroyed))
        return;
    disconnect(m_nodeDestroyed);
    m_nodeDestroyed = {};
    failPending();
    if (m_state) {
        disconnect(m_state, nullptr, this, nullptr);
        m_state->deleteLater();
    }
    m_node = node;
    m_state = node ? node->acquire<BagStateReplica>() : new BagStateReplica;
    m_state->setParent(this);
    if (node)
        m_nodeDestroyed = connect(node, &QObject::destroyed, this, [this] { setNode(nullptr); });

    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &BagService::changed);
    connect(m_state, &QRemoteObjectReplica::notified, this, &BagService::changed);
    connect(m_state, &BagStateReplica::inventoryChanged, this, &BagService::changed);
    connect(m_state, &BagStateReplica::finishedBagsChanged, this, &BagService::stateChanged);
    connect(m_state, &BagStateReplica::activeBagIdChanged, this, &BagService::stateChanged);
    connect(m_state, &BagStateReplica::inventoryReadyChanged, this, &BagService::changed);
    connect(m_state, &BagStateReplica::finishedBagsReadyChanged, this, &BagService::stateChanged);
    connect(m_state, &BagStateReplica::loadingChanged, this, &BagService::stateChanged);
    connect(m_state, &BagStateReplica::errorMessageChanged, this, &BagService::stateChanged);
    connect(m_state, &BagStateReplica::inventoryResult, this,
        [this](const QString& id, bool success, const QVariantList& rows, const QString& reason) {
            if (m_reads.value(id).operation != QLatin1String("inventory"))
                return;
            m_reads.remove(id);
            if (success) emit inventoryReady(rows);
            else emit inventoryFailed();
            if (!success && !reason.isEmpty()) emit errorOccurred(reason);
        }, Qt::QueuedConnection);
    connect(m_state, &BagStateReplica::finishedBagsResult, this,
        [this](const QString& id, bool success, const QVariantList& rows, const QString& reason) {
            if (m_reads.value(id).operation != QLatin1String("finished"))
                return;
            m_reads.remove(id);
            if (success) emit finishedBagsReady(rows);
            else emit finishedBagsFailed();
            if (!success && !reason.isEmpty()) emit errorOccurred(reason);
        }, Qt::QueuedConnection);
    connect(m_state, &BagStateReplica::finishedBagCountResult, this,
        [this](const QString& id, bool success, int count, const QString& reason) {
            if (m_reads.value(id).operation != QLatin1String("finishedCount"))
                return;
            m_reads.remove(id);
            if (success) emit finishedBagCountReady(count);
            else emit finishedBagCountFailed();
            if (!success && !reason.isEmpty()) emit errorOccurred(reason);
        }, Qt::QueuedConnection);
    connect(m_state, &BagStateReplica::bagResult, this,
        [this](const QString& id, qint64 bagId, bool success, const QVariantMap& bag, const QString& reason) {
            const auto it = m_reads.find(id);
            if (it == m_reads.end() || it->operation != QLatin1String("bag") || it->bagId != bagId)
                return;
            m_reads.erase(it);
            if (success || reason == QLatin1String("notFound"))
                emit bagReady(bagId, bag);
            else {
                emit bagReadFailed(bagId);
                if (!reason.isEmpty()) emit errorOccurred(reason);
            }
        }, Qt::QueuedConnection);
    connect(m_state, &BagStateReplica::operationCompleted, this,
        [this](const QString& id, const QString&, qint64 bagId, bool success,
               const QVariantMap& result, const QString& reason) {
            // The complete tuple is routed by ServiceOperation's wire ID.
            const auto pendingId = m_operation.busy() ? id : QString();
            if (!pendingId.isEmpty())
                m_operation.finish(id, success, bagId, reason, result);
        }, Qt::QueuedConnection);
    connect(m_state, &BagStateReplica::bagRestocked, this, &BagService::bagRestocked);
    connect(m_state, &BagStateReplica::bagFinished, this, &BagService::bagFinished);
    connect(m_state, &BagStateReplica::bagPulledFromVisualizer,
            this, &BagService::bagPulledFromVisualizer);
    connect(m_state, &BagStateReplica::bagVisualizerFieldsChanged,
            this, &BagService::bagVisualizerFieldsChanged);
    connect(m_state, &BagStateReplica::bagsChanged, this, [this] { emit bagsChanged(); });
    changed();
}

QString BagService::newRequestId() const { return ServiceOperation::newId(); }

void BagService::changed()
{
    if (!connected())
        failPending();
    const bool inventoryReadyNow = ready();
    const QVariantList rows = bags();
    if (inventoryReadyNow && (!m_inventoryWasReady || rows != m_notifiedInventory)) {
        m_notifiedInventory = rows;
        emit inventoryReady(rows);
    }
    m_inventoryWasReady = inventoryReadyNow;
    emit stateChanged();
}

void BagService::refresh() { requestInventory(); }

void BagService::requestInventory()
{
    if (!connected()) {
        emit inventoryFailed();
        return;
    }
    const QString requestId = ServiceOperation::newId();
    m_reads.insert(requestId, {QStringLiteral("inventory"), -1});
    m_state->refresh(requestId);
}

void BagService::requestFinishedBags()
{
    if (!connected()) {
        emit finishedBagsFailed();
        return;
    }
    const QString requestId = ServiceOperation::newId();
    m_reads.insert(requestId, {QStringLiteral("finished"), -1});
    m_state->refreshFinished(requestId);
}

void BagService::requestFinishedBagCount()
{
    if (!connected()) {
        emit finishedBagCountFailed();
        emit errorOccurred(errorMessage());
        return;
    }
    const QString requestId = ServiceOperation::newId();
    m_reads.insert(requestId, {QStringLiteral("finishedCount"), -1});
    m_state->refreshFinishedCount(requestId);
}

void BagService::requestBag(qint64 bagId)
{
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, bagId] { emit bagReadFailed(bagId); }, Qt::QueuedConnection);
        return;
    }
    const QString requestId = ServiceOperation::newId();
    m_reads.insert(requestId, {QStringLiteral("bag"), bagId});
    m_state->requestBag(requestId, bagId);
}

void BagService::requestCreateBag(const QVariantMap& bag) { send(QStringLiteral("create"), -1, bag, false); }
QString BagService::requestUpdateBag(qint64 bagId, const QVariantMap& fields, bool propagateBeanBase,
                                     const QVariantMap& expectedEditable, qint64 expectedActiveBagId)
{ return send(QStringLiteral("update"), bagId, fields, propagateBeanBase, expectedEditable, expectedActiveBagId); }
void BagService::requestMarkEmpty(qint64 bagId) { send(QStringLiteral("markEmpty"), bagId, {}, false); }
void BagService::requestMarkAiPageSearched(qint64 bagId)
{ send(QStringLiteral("markAiPageSearched"), bagId, {}, false); }
void BagService::requestTouchLastUsed(qint64 bagId) { send(QStringLiteral("touchLastUsed"), bagId, {}, false); }
void BagService::requestActivateBag(qint64 bagId) { send(QStringLiteral("activate"), bagId, {}, false); }
void BagService::requestDeleteBag(qint64 bagId) { send(QStringLiteral("delete"), bagId, {}, false); }

QString BagService::send(const QString& operation, qint64 bagId, const QVariantMap& fields,
                         bool propagateBeanBase, const QVariantMap& expectedEditable,
                         qint64 expectedActiveBagId)
{
    const QString uiRequestId = ServiceOperation::newId();
    const QString requestId = m_operation.begin(operation, bagId, uiRequestId, ready());
    if (!requestId.isEmpty()) {
        m_state->execute(requestId, operation, bagId, fields, propagateBeanBase,
                         expectedEditable, expectedActiveBagId);
    }
    return uiRequestId;
}

void BagService::report(const QString& requestId, const QString& operation, qint64 bagId,
                        bool success, const QVariantMap& result, const QString& reason)
{
    emit operationCompleted(requestId, operation, bagId, success, result, reason);
    if (!success) {
        emit operationFailed(bagId, operation, reason);
        if (!reason.isEmpty()) emit errorOccurred(reason);
    }
    if (operation == QLatin1String("create")) {
        const qint64 createdId = success ? result.value(QStringLiteral("id")).toLongLong() : -1;
        emit bagCreated(createdId, success ? result : QVariantMap());
    } else if (operation == QLatin1String("update") || operation == QLatin1String("markEmpty")
               || operation == QLatin1String("restore")) {
        emit bagUpdated(bagId, success, result.value(QStringLiteral("refusal")).toString());
    } else if (operation == QLatin1String("delete")) {
        emit bagDeleted(bagId, success);
    } else if (operation == QLatin1String("activate")) {
        emit bagActivated(bagId, success, success ? result : QVariantMap(), success ? QString() : reason);
    }
}

QVariantList BagService::storageHintOptions() const { return CoffeeBagFields::storageHintOptions(); }

QVariantList BagService::lifecycleParts(const QVariantMap& bag, const QString& referenceIso) const
{
    const QDate reference = QDate::fromString(referenceIso, QStringLiteral("yyyy-MM-dd"));
    return CoffeeBagFields::lifecycleParts(bag, reference.isValid() ? reference : QDate::currentDate());
}

QVariantMap BagService::restockTemplate(const QVariantMap& bag) const
{ return CoffeeBagFields::restockTemplate(bag, QDate::currentDate()); }

QVariantMap BagService::editChanges(const QVariantMap& opened, const QVariantMap& current,
                                    bool replaceBlob) const
{ return CoffeeBagFields::editChanges(opened, current, replaceBlob); }

void BagService::failPending()
{
    m_operation.disconnectPending();
    const auto reads = std::exchange(m_reads, {});
    for (auto it = reads.cbegin(); it != reads.cend(); ++it) {
        const auto op = it.value().operation;
        const qint64 bagId = it.value().bagId;
        QMetaObject::invokeMethod(this, [this, op, bagId] {
            if (op == QLatin1String("inventory")) emit inventoryFailed();
            else if (op == QLatin1String("finished")) emit finishedBagsFailed();
            else if (op == QLatin1String("finishedCount")) emit finishedBagCountFailed();
            else emit bagReadFailed(bagId);
        }, Qt::QueuedConnection);
    }
}
