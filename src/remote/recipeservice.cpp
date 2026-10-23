#include "recipeservice.h"
#include "core/recipeeditfields.h"
#include <QRemoteObjectNode>
#include <utility>

namespace { QPointer<QRemoteObjectNode> s_recipeNode; }

RecipeService::RecipeService(QRemoteObjectNode* node, QObject* parent) : RemoteService(parent)
{
    connect(&m_operation, &ServiceOperation::busyChanged, this, &RecipeService::busyChanged);
    connect(&m_operation, &ServiceOperation::errorChanged, this, &RecipeService::stateChanged);
    connect(&m_operation, &ServiceOperation::completed, this, &RecipeService::report);
    setNode(node);
}
RecipeService::~RecipeService()
{
    if (m_state) disconnect(m_state, nullptr, this, nullptr);
}
void RecipeService::setPublishedNode(QRemoteObjectNode* node) { s_recipeNode = node; }
QRemoteObjectNode* RecipeService::publishedNode() { return s_recipeNode; }
bool RecipeService::connected() const
{
    return RemoteServiceReplica::isReady(m_node, m_state, s_recipeNode);
}
bool RecipeService::ready() const { return connected() && m_state->inventoryReady(); }
bool RecipeService::loading() const { return connected() && m_state->loading(); }
QVariantList RecipeService::recipes() const { return connected() ? m_state->recipes() : QVariantList(); }
QVariantList RecipeService::archivedRecipes() const { return connected() ? m_state->archivedRecipes() : QVariantList(); }
qint64 RecipeService::activeRecipeId() const { return connected() ? m_state->activeRecipeId() : -1; }
QString RecipeService::errorMessage() const
{
    return connected() ? m_state->errorMessage()
        : tr("Recipes are disconnected. Reconnect to refresh their state.");
}
QVariantMap RecipeService::editableSnapshot(const QVariantMap& recipe) const
{
    QVariantMap snapshot;
    RecipeEditFields::projectSnapshot(recipe, &snapshot);
    return snapshot;
}
bool RecipeService::isSaveValid(const QString& name, const QString& title, const QString& water) const
{
    return RecipeEditFields::saveValidationPasses(name, title, water);
}
void RecipeService::setNode(QRemoteObjectNode* node)
{
    if (!RemoteServiceReplica::replace(this, node, m_node, m_state, m_nodeDestroyed,
            [this] { failPending(); }))
        return;
    connect(m_state, &QRemoteObjectReplica::stateChanged, this, &RecipeService::changed);
    connect(m_state, &QRemoteObjectReplica::notified, this, &RecipeService::changed);
    connect(m_state, &RecipeStateReplica::recipesChanged, this, &RecipeService::changed);
    connect(m_state, &RecipeStateReplica::archivedRecipesChanged, this, &RecipeService::changed);
    connect(m_state, &RecipeStateReplica::inventoryReadyChanged, this, &RecipeService::changed);
    connect(m_state, &RecipeStateReplica::loadingChanged, this, &RecipeService::changed);
    connect(m_state, &RecipeStateReplica::errorMessageChanged, this, &RecipeService::changed);
    connect(m_state, &RecipeStateReplica::activeRecipeIdChanged, this, &RecipeService::stateChanged);
    connect(m_state, &RecipeStateReplica::operationCompleted, this,
        [this](const QString& id, const QString&, bool ok, qint64 resultId,
               const QString& reason, const QVariantMap& result) {
            m_operation.finish(id, ok, resultId, reason, result);
        }, Qt::QueuedConnection);
    connect(m_state, &RecipeStateReplica::recipeResult, this,
        [this](const QString& token, qint64 id, const QVariantMap& row, const QString& reason) {
            if (!m_reads.remove(token)) return;
            if (reason.isEmpty() || reason == QLatin1String("notFound")) emit recipeReady(id, row);
            else emit recipeCheckFailed(id);
        }, Qt::QueuedConnection);
    connect(m_state, &RecipeStateReplica::lastEquipmentForDrinkTypeResult, this,
        [this](const QString& token, const QString& type, qint64 id, const QString& reason) {
            if (!m_equipmentReads.remove(token)) return;
            if (reason.isEmpty()) emit lastEquipmentForDrinkTypeReady(type, id);
            else emit lastEquipmentForDrinkTypeFailed(type);
        }, Qt::QueuedConnection);
    connect(m_state, &RecipeStateReplica::countRecipesUsingProfileResult, this,
        [this](const QString& token, const QString& profileTitle, int count,
               const QString& reason) {
            const auto it = m_profileCountReads.find(token);
            if (it == m_profileCountReads.end())
                return;
            const QString requestedTitle = it.value();
            m_profileCountReads.erase(it);
            if (requestedTitle != profileTitle || count < 0 || !reason.isEmpty()) {
                emit countRecipesUsingProfileReady(
                    token, requestedTitle, -1,
                    reason.isEmpty() ? QStringLiteral("invalidResponse") : reason);
                return;
            }
            emit countRecipesUsingProfileReady(token, requestedTitle, count, reason);
        }, Qt::QueuedConnection);
    changed();
}
void RecipeService::changed()
{
    if (!connected()) failPending();
    emit stateChanged();
    const auto active = recipes();
    const auto archived = archivedRecipes();
    if (active != m_notifiedRecipes || archived != m_notifiedArchived) {
        m_notifiedRecipes = active;
        m_notifiedArchived = archived;
        emit recipesChanged();
    }
    if (ready() && !loading()) {
        emit inventoryReady(active);
        emit archivedReady(archived);
    } else if (connected() && !loading() && !m_state->errorMessage().isEmpty()) {
        emit inventoryFailed(m_state->errorMessage());
    }
}
void RecipeService::requestInventory()
{
    if (m_refreshQueued) return;
    m_refreshQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_refreshQueued = false;
        if (connected()) { m_operation.clearError(); m_state->refresh(); }
        else emit inventoryFailed(errorMessage());
    }, Qt::QueuedConnection);
}
void RecipeService::requestArchived() { requestInventory(); }
void RecipeService::requestRecipe(qint64 id)
{
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, id] { emit recipeCheckFailed(id); }, Qt::QueuedConnection);
        return;
    }
    const auto token = ServiceOperation::newId();
    m_reads.insert(token, id);
    m_state->requestRecipe(token, id);
}
void RecipeService::requestLastEquipmentForDrinkType(const QString& type)
{
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, type] { emit lastEquipmentForDrinkTypeFailed(type); }, Qt::QueuedConnection);
        return;
    }
    const auto token = ServiceOperation::newId();
    m_equipmentReads.insert(token, type);
    m_state->requestLastEquipmentForDrinkType(token, type);
}
QString RecipeService::requestCountRecipesUsingProfile(const QString& profileTitle)
{
    const QString token = ServiceOperation::newId();
    if (!connected()) {
        QMetaObject::invokeMethod(this, [this, token, profileTitle] {
            emit countRecipesUsingProfileReady(token, profileTitle, -1,
                                               QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
        return token;
    }
    m_profileCountReads.insert(token, profileTitle);
    m_state->requestCountRecipesUsingProfile(token, profileTitle);
    return token;
}
void RecipeService::requestCreateRecipe(const QVariantMap& fields)
{
    auto values = fields;
    const auto token = values.take(QStringLiteral("requestToken")).toString();
    send(QStringLiteral("create"), -1, values, {}, token);
}
QString RecipeService::requestUpdateRecipe(qint64 id, const QVariantMap& fields, const QVariantMap& expected)
{
    const QString requestId = ServiceOperation::newId();
    send(QStringLiteral("update"), id, fields, expected, requestId);
    return requestId;
}
void RecipeService::requestCloneRecipe(qint64 id, const QString& name, const QString& token)
{ send(QStringLiteral("clone"), id, {{QStringLiteral("name"), name}}, {}, token); }
void RecipeService::requestArchiveRecipe(qint64 id) { send(QStringLiteral("archive"), id, {}, snapshotFor(id)); }
void RecipeService::requestUnarchiveRecipe(qint64 id) { send(QStringLiteral("restore"), id, {}, snapshotFor(id)); }
void RecipeService::requestDeleteRecipe(qint64 id) { send(QStringLiteral("delete"), id); }
void RecipeService::requestRelinkRecipeToBag(qint64 id, qint64 bagId)
{ send(QStringLiteral("relink"), id, {{QStringLiteral("bagId"), bagId}}, snapshotFor(id)); }
QString RecipeService::requestActivateRecipe(qint64 id)
{
    const QString requestId = ServiceOperation::newId();
    send(QStringLiteral("activate"), id, {}, {}, requestId);
    return requestId;
}
void RecipeService::requestDeactivateRecipe() { send(QStringLiteral("deactivate"), -1); }
QVariantMap RecipeService::snapshotFor(qint64 id) const
{
    for (const auto& rows : {recipes(), archivedRecipes()}) {
        for (const QVariant& value : rows) {
            const auto row = value.toMap();
            if (row.value(QStringLiteral("id")).toLongLong() == id)
                return editableSnapshot(row);
        }
    }
    return {};
}
void RecipeService::send(const QString& operation, qint64 id, const QVariantMap& fields,
                          const QVariantMap& expected, const QString& uiId)
{
    const auto wireId = m_operation.begin(operation, id, uiId, ready());
    if (!wireId.isEmpty()) m_state->execute(wireId, operation, id, fields, expected);
}
void RecipeService::report(const QString& uiId, const QString& operation, qint64 originalId,
                            bool success, qint64 resultId, const QString& reason,
                            const QVariantMap& result)
{
    if (operation == QLatin1String("create") || operation == QLatin1String("clone")) {
        auto row = result;
        row.insert(QStringLiteral("requestToken"), uiId);
        emit recipeCreated(success ? resultId : -1, row);
    } else if (operation == QLatin1String("delete")) {
        emit recipeDeleted(originalId, success);
    } else if (operation == QLatin1String("activate")) {
        emit recipeActivated(originalId, success);
    } else if (operation != QLatin1String("deactivate")) {
        if (!success) emit recipeUpdateFailed(originalId, reason);
        emit recipeUpdated(originalId, success);
    }
    emit operationCompleted(uiId, operation, success, resultId, reason);
}
void RecipeService::failPending()
{
    m_operation.disconnectPending();
    const auto reads = std::exchange(m_reads, {});
    for (auto id : reads)
        QMetaObject::invokeMethod(this, [this, id] { emit recipeCheckFailed(id); }, Qt::QueuedConnection);
    const auto equipment = std::exchange(m_equipmentReads, {});
    for (const auto& type : equipment)
        QMetaObject::invokeMethod(this, [this, type] { emit lastEquipmentForDrinkTypeFailed(type); }, Qt::QueuedConnection);
    const auto profileCounts = std::exchange(m_profileCountReads, {});
    for (auto it = profileCounts.cbegin(); it != profileCounts.cend(); ++it) {
        const QString requestId = it.key();
        const QString title = it.value();
        QMetaObject::invokeMethod(this, [this, requestId, title] {
            emit countRecipesUsingProfileReady(requestId, title, -1,
                                               QStringLiteral("disconnected"));
        }, Qt::QueuedConnection);
    }
}
