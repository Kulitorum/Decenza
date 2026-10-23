#include "recipeservicesource.h"

#include "core/recipeeditfields.h"
#include "core/settings_dye.h"
#include "history/recipestorage.h"

#include <QMetaObject>
#include <QMetaType>
#include <QSet>

#include <optional>
#include <utility>

namespace {
QString operationReason(const QString& reason)
{
    if (reason == QLatin1String("expectedEditableConflict")
        || reason == QLatin1String("nameConflict"))
        return QStringLiteral("conflict");
    return reason;
}

bool normalizeEditableSnapshot(QVariantMap* snapshot)
{
    if (!snapshot)
        return false;
    QVariantMap normalized;
    if (!RecipeEditFields::hasExactSnapshot(*snapshot, &normalized))
        return false;
    *snapshot = std::move(normalized);
    return true;
}

bool onlyField(const QVariantMap& fields, const QString& key)
{
    return fields.size() == 1 && fields.contains(key);
}

bool validCreateFields(QVariantMap* fields)
{
    if (!fields)
        return false;
    static const QSet<QString> provenance{
        QStringLiteral("createdFromShotId"), QStringLiteral("clonedFromRecipeId")
    };
    QSet<QString> createFields;
    for (const QString& key : RecipeEditFields::keys()) {
        if (key == QLatin1String("archived"))
            continue;
        createFields.insert(key);
        if (!fields->contains(key))
            return false;
    }
    for (auto it = fields->cbegin(); it != fields->cend(); ++it) {
        if (!createFields.contains(it.key()) && !provenance.contains(it.key()))
            return false;
    }

    QVariantMap editable = *fields;
    editable.insert(QStringLiteral("archived"), false);
    QVariantMap normalized;
    if (!RecipeEditFields::projectSnapshot(editable, &normalized)
        || !RecipeEditFields::saveValidationPasses(
            normalized.value(QStringLiteral("name")).toString(),
            normalized.value(QStringLiteral("profileTitle")).toString(),
            normalized.value(QStringLiteral("hotWaterJson")).toString()))
        return false;

    for (const QString& key : provenance) {
        if (!fields->contains(key))
            continue;
        qint64 id = 0;
        if (!RecipeEditFields::integerValue(fields->value(key), &id) || id <= 0)
            return false;
        normalized.insert(key, QVariant::fromValue(id));
    }
    *fields = std::move(normalized);
    return true;
}

bool validUpdatePatch(const QVariantMap& fields, const QVariantMap& expected,
                      QVariantMap* normalizedFields)
{
    if (fields.isEmpty() || !normalizedFields)
        return false;
    QVariantMap merged = expected;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (it.key() == QLatin1String("archived")
            || !RecipeEditFields::keys().contains(it.key()))
            return false;
        merged.insert(it.key(), it.value());
    }
    QVariantMap normalized;
    if (!RecipeEditFields::hasExactSnapshot(merged, &normalized))
        return false;
    normalizedFields->clear();
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
        normalizedFields->insert(it.key(), normalized.value(it.key()));
    return true;
}

bool positiveInteger(const QVariant& value, qint64* result)
{
    return RecipeEditFields::integerValue(value, result) && *result > 0;
}
} // namespace

NativeRecipeServiceSource::NativeRecipeServiceSource(RecipeStorage* storage,
                                                     SettingsDye* dye,
                                                     ActivateRecipe activateRecipe,
                                                     DeactivateRecipe deactivateRecipe,
                                                     QObject* parent)
    : RecipeStateSimpleSource(parent)
    , m_storage(storage)
    , m_dye(dye)
    , m_activateRecipe(std::move(activateRecipe))
    , m_deactivateRecipe(std::move(deactivateRecipe))
{
    if (m_storage) {
        connect(m_storage, &RecipeStorage::recipesChanged, this, [this] {
            if (m_refreshQueued)
                return;
            m_refreshQueued = true;
            QMetaObject::invokeMethod(this, [this] {
                m_refreshQueued = false;
                refresh();
            }, Qt::QueuedConnection);
        });
    }
    if (m_dye) {
        connect(m_dye, &SettingsDye::activeRecipeIdChanged,
                this, &NativeRecipeServiceSource::onActiveRecipeChanged);
    }
    onActiveRecipeChanged();
    refresh();
}

void NativeRecipeServiceSource::refresh()
{
    if (m_inventoryReadInFlight) {
        m_inventoryDirty = true;
        return;
    }
    if (!m_storage) {
        setErrorMessage(QStringLiteral("Recipe storage is unavailable"));
        setInventoryReady(false);
        setLoading(false);
        emit inventoryFailed(errorMessage());
        return;
    }

    setErrorMessage(QString());
    setLoading(true);
    m_inventoryReadInFlight = true;
    m_inventoryDirty = false;
    m_inventoryReadFailed = false;
    m_inventoryFailureReason.clear();
    m_pendingRecipes.clear();
    m_pendingArchivedRecipes.clear();
    m_inventoryReadsRemaining = 2;
    const quint64 generation = ++m_inventoryGeneration;

    m_storage->requestInventoryWithCompletion(this,
        [this, generation](bool success, const QVariantList& rows, const QString& reason) {
            finishInventoryRead(generation, false, success, rows, reason);
        }, false);
    m_storage->requestInventoryWithCompletion(this,
        [this, generation](bool success, const QVariantList& rows, const QString& reason) {
            finishInventoryRead(generation, true, success, rows, reason);
        }, true);
}

void NativeRecipeServiceSource::finishInventoryRead(quint64 generation, bool archived,
                                                    bool success, const QVariantList& rows,
                                                    const QString& reason)
{
    if (generation != m_inventoryGeneration || !m_inventoryReadInFlight)
        return;
    if (success) {
        if (archived)
            m_pendingArchivedRecipes = rows;
        else
            m_pendingRecipes = rows;
    } else {
        m_inventoryReadFailed = true;
        if (m_inventoryFailureReason.isEmpty())
            m_inventoryFailureReason = reason.isEmpty()
                ? QStringLiteral("Recipe inventory read failed") : reason;
    }
    if (--m_inventoryReadsRemaining == 0)
        finishInventoryBatch(generation);
}

void NativeRecipeServiceSource::finishInventoryBatch(quint64 generation)
{
    if (generation != m_inventoryGeneration || !m_inventoryReadInFlight)
        return;
    m_inventoryReadInFlight = false;
    if (m_inventoryDirty) {
        m_inventoryDirty = false;
        refresh();
        return;
    }
    if (m_inventoryReadFailed) {
        setErrorMessage(m_inventoryFailureReason);
        setInventoryReady(false);
        setLoading(false);
        emit inventoryFailed(m_inventoryFailureReason);
        return;
    }
    m_recipes = m_pendingRecipes;
    m_archivedRecipes = m_pendingArchivedRecipes;
    setRecipes(m_recipes);
    setArchivedRecipes(m_archivedRecipes);
    setErrorMessage(QString());
    setInventoryReady(true);
    setLoading(false);
}

void NativeRecipeServiceSource::requestRecipe(QString requestId, qint64 recipeId)
{
    const ServiceRequestIds::Start start = m_requestIds.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId)
        return;
    if (start != ServiceRequestIds::Start::Accepted) {
        rejectRequest(requestId, QStringLiteral("read"), recipeId,
                      start == ServiceRequestIds::Start::LimitReached
                          ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId"));
        return;
    }
    if (!m_storage || recipeId <= 0) {
        if (m_requestIds.finish(requestId))
            emit recipeResult(requestId, recipeId, {}, m_storage
                ? QStringLiteral("invalidRecipeId") : QStringLiteral("disconnected"));
        return;
    }
    m_storage->requestRecipeWithCompletion(recipeId, this,
        [this, requestId](qint64 resultId, bool success, const QVariantMap& recipe,
                          const QString& reason) {
            if (!m_requestIds.finish(requestId))
                return;
            emit recipeResult(requestId, resultId, recipe,
                              success ? QString() : (reason.isEmpty()
                                  ? QStringLiteral("recipeReadFailed") : reason));
        });
}

void NativeRecipeServiceSource::requestLastEquipmentForDrinkType(QString requestId,
                                                                 QString drinkType)
{
    const ServiceRequestIds::Start start = m_requestIds.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId)
        return;
    if (start != ServiceRequestIds::Start::Accepted) {
        rejectRequest(requestId, QStringLiteral("lastEquipmentForDrinkType"), 0,
                      start == ServiceRequestIds::Start::LimitReached
                          ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId"),
                      drinkType);
        return;
    }
    if (!m_storage || drinkType.trimmed().isEmpty()) {
        m_requestIds.finish(requestId);
        emit lastEquipmentForDrinkTypeResult(requestId, drinkType, 0,
            m_storage ? QStringLiteral("invalidDrinkType") : QStringLiteral("disconnected"));
        return;
    }
    if (drinkType != drinkType.trimmed()) {
        m_requestIds.finish(requestId);
        emit lastEquipmentForDrinkTypeResult(requestId, drinkType, 0,
                                              QStringLiteral("invalidDrinkType"));
        return;
    }
    m_storage->requestLastEquipmentForDrinkTypeWithCompletion(
        drinkType, this,
        [this, requestId](const QString& resultType, qint64 equipmentId,
                          bool success, const QString& reason) {
            if (!m_requestIds.finish(requestId))
                return;
            emit lastEquipmentForDrinkTypeResult(
                requestId, resultType, equipmentId,
                success ? QString() : (reason.isEmpty()
                    ? QStringLiteral("readFailed") : reason));
        });
}

void NativeRecipeServiceSource::requestCountRecipesUsingProfile(QString requestId,
                                                                QString profileTitle)
{
    const ServiceRequestIds::Start start = m_requestIds.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId)
        return;
    if (start != ServiceRequestIds::Start::Accepted) {
        const QString reason = start == ServiceRequestIds::Start::LimitReached
            ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId");
        QMetaObject::invokeMethod(this, [this, requestId, profileTitle, reason] {
            if (!requestId.isEmpty())
                emit countRecipesUsingProfileResult(requestId, profileTitle,
                                                     kRecipeCountUnknown, reason);
        }, Qt::QueuedConnection);
        return;
    }
    if (profileTitle.size() > 512 || profileTitle.contains(QChar::Null)) {
        m_requestIds.finish(requestId);
        emit countRecipesUsingProfileResult(requestId, profileTitle,
                                             kRecipeCountUnknown,
                                             QStringLiteral("invalidProfileTitle"));
        return;
    }
    if (!m_storage) {
        m_requestIds.finish(requestId);
        emit countRecipesUsingProfileResult(requestId, profileTitle,
                                             kRecipeCountUnknown,
                                             QStringLiteral("disconnected"));
        return;
    }
    m_storage->requestCountRecipesUsingProfileWithCompletion(
        profileTitle, this,
        [this, requestId, profileTitle](bool success, int count, const QString& reason) {
            if (!m_requestIds.finish(requestId))
                return;
            emit countRecipesUsingProfileResult(
                requestId, profileTitle, success ? count : kRecipeCountUnknown,
                success ? QString() : (reason.isEmpty()
                    ? QStringLiteral("readFailed") : reason));
        });
}

void NativeRecipeServiceSource::execute(QString requestId, QString operation,
                                        qint64 recipeId, QVariantMap fields,
                                        QVariantMap expectedEditable)
{
    const ServiceRequestIds::Start start = m_requestIds.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId)
        return;
    if (start != ServiceRequestIds::Start::Accepted) {
        rejectOperation(requestId, operation, recipeId,
                        start == ServiceRequestIds::Start::LimitReached
                            ? QStringLiteral("tooManyRequests") : QStringLiteral("invalidRequestId"));
        return;
    }
    if (!m_storage) {
        finishOperation(requestId, operation, false, recipeId, QStringLiteral("disconnected"));
        return;
    }

    if (operation == QLatin1String("create")) {
        if (recipeId > 0 || !expectedEditable.isEmpty() || !validCreateFields(&fields)) {
            finishOperation(requestId, operation, false, recipeId, QStringLiteral("invalidRequest"));
            return;
        }
        m_storage->requestCreateRecipeWithCompletion(fields, this,
            [this, requestId, operation](qint64 resultId, bool success,
                                         const QVariantMap& result, const QString& reason) {
                finishOperation(requestId, operation, success, resultId,
                                operationReason(reason), result);
            });
        return;
    }

    if (operation == QLatin1String("clone")) {
        if (recipeId <= 0 || !onlyField(fields, QStringLiteral("name"))
            || fields.value(QStringLiteral("name")).metaType().id() != QMetaType::QString
            || fields.value(QStringLiteral("name")).toString().trimmed().isEmpty()
            || !expectedEditable.isEmpty()) {
            finishOperation(requestId, operation, false, recipeId, QStringLiteral("invalidRequest"));
            return;
        }
        m_storage->requestCloneRecipeWithCompletion(recipeId,
            fields.value(QStringLiteral("name")).toString(), requestId, this,
            [this, requestId, operation](qint64 resultId, bool success,
                                         const QVariantMap& result, const QString& reason) {
                finishOperation(requestId, operation, success, resultId,
                                operationReason(reason), result);
            });
        return;
    }

    if (operation == QLatin1String("update")
        || operation == QLatin1String("archive")
        || operation == QLatin1String("restore")
        || operation == QLatin1String("relink")) {
        if (recipeId <= 0 || !normalizeEditableSnapshot(&expectedEditable)) {
            finishOperation(requestId, operation, false, recipeId,
                            QStringLiteral("invalidExpectedEditable"));
            return;
        }
        if (operation == QLatin1String("archive")
            || operation == QLatin1String("restore")) {
            if (!fields.isEmpty()) {
                finishOperation(requestId, operation, false, recipeId,
                                QStringLiteral("invalidRequest"));
                return;
            }
        }
        if (operation == QLatin1String("archive"))
            fields = {{QStringLiteral("archived"), true}};
        else if (operation == QLatin1String("restore"))
            fields = {{QStringLiteral("archived"), false}};
        else if (operation == QLatin1String("relink")) {
            qint64 bagId = 0;
            if (!onlyField(fields, QStringLiteral("bagId"))
                || !positiveInteger(fields.value(QStringLiteral("bagId")), &bagId)) {
                finishOperation(requestId, operation, false, recipeId,
                                QStringLiteral("invalidRequest"));
                return;
            }
            fields.insert(QStringLiteral("bagId"), QVariant::fromValue(bagId));
        } else if (operation == QLatin1String("update")) {
            QVariantMap normalizedFields;
            if (!validUpdatePatch(fields, expectedEditable, &normalizedFields)) {
                finishOperation(requestId, operation, false, recipeId,
                                QStringLiteral("invalidRequest"));
                return;
            }
            fields = std::move(normalizedFields);
        }
        if (fields.isEmpty()) {
            finishOperation(requestId, operation, false, recipeId, QStringLiteral("invalidRequest"));
            return;
        }
        const bool deactivateActiveRecipeOnSuccess =
            operation == QLatin1String("archive")
            && m_dye && m_dye->activeRecipeId() == recipeId;
        if (deactivateActiveRecipeOnSuccess && !m_deactivateRecipe) {
            finishOperation(requestId, operation, false, recipeId,
                            QStringLiteral("disconnected"));
            return;
        }
        m_storage->requestUpdateRecipeWithCompletion(recipeId, fields, this,
            [this, requestId, operation, recipeId, deactivateActiveRecipeOnSuccess]
            (qint64 resultId, bool success, const QString& reason, const QString& currentName) {
                if (success && deactivateActiveRecipeOnSuccess && m_dye
                    && m_dye->activeRecipeId() == recipeId && m_deactivateRecipe)
                    m_deactivateRecipe();
                QVariantMap result;
                if (!currentName.isNull())
                    result.insert(QStringLiteral("currentName"), currentName);
                finishOperation(requestId, operation, success, resultId,
                                operationReason(reason), result);
            }, std::nullopt, std::optional<QVariantMap>(expectedEditable));
        return;
    }

    if (operation == QLatin1String("delete")) {
        if (recipeId <= 0 || !fields.isEmpty() || !expectedEditable.isEmpty()) {
            finishOperation(requestId, operation, false, recipeId, QStringLiteral("invalidRequest"));
            return;
        }
        const bool deactivateActiveRecipeOnSuccess =
            m_dye && m_dye->activeRecipeId() == recipeId;
        if (deactivateActiveRecipeOnSuccess && !m_deactivateRecipe) {
            finishOperation(requestId, operation, false, recipeId,
                            QStringLiteral("disconnected"));
            return;
        }
        m_storage->requestDeleteRecipeWithCompletion(recipeId, this,
            [this, requestId, operation, recipeId, deactivateActiveRecipeOnSuccess]
            (qint64 resultId, bool success, const QString& reason) {
                if (success && deactivateActiveRecipeOnSuccess && m_dye
                    && m_dye->activeRecipeId() == recipeId && m_deactivateRecipe)
                    m_deactivateRecipe();
                finishOperation(requestId, operation, success, resultId,
                                operationReason(reason));
            });
        return;
    }

    if (operation == QLatin1String("activate")) {
        if (recipeId <= 0 || !fields.isEmpty() || !expectedEditable.isEmpty()
            || !m_activateRecipe) {
            finishOperation(requestId, operation, false, recipeId,
                            m_activateRecipe ? QStringLiteral("invalidRequest") : QStringLiteral("disconnected"));
            return;
        }
        if (!m_activationRequestId.isEmpty()) {
            finishOperation(requestId, operation, false, recipeId, QStringLiteral("busy"));
            return;
        }
        m_activationRequestId = requestId;
        m_activationRecipeId = recipeId;
        m_activateRecipe(recipeId);
        return;
    }

    if (operation == QLatin1String("deactivate")) {
        if (!fields.isEmpty() || !expectedEditable.isEmpty() || !m_deactivateRecipe) {
            finishOperation(requestId, operation, false, recipeId,
                            m_deactivateRecipe ? QStringLiteral("invalidRequest") : QStringLiteral("disconnected"));
            return;
        }
        m_deactivateRecipe();
        finishOperation(requestId, operation, true, -1, QString());
        return;
    }

    finishOperation(requestId, operation, false, recipeId, QStringLiteral("unknownOperation"));
}

void NativeRecipeServiceSource::finishOperation(const QString& requestId,
                                                const QString& operation,
                                                bool success, qint64 resultId,
                                                const QString& reason,
                                                const QVariantMap& result)
{
    if (!m_requestIds.finish(requestId))
        return;
    emit operationCompleted(requestId, operation, success, resultId,
                            operationReason(reason), result);
}

void NativeRecipeServiceSource::rejectRequest(const QString& requestId,
                                              const QString& operation,
                                              qint64 recipeId,
                                              const QString& reason,
                                              const QString& drinkType)
{
    if (requestId.isEmpty())
        return;
    QMetaObject::invokeMethod(this, [this, requestId, operation, recipeId, reason, drinkType] {
        if (operation == QLatin1String("read")) {
            emit recipeResult(requestId, recipeId, {}, reason);
        } else {
            emit lastEquipmentForDrinkTypeResult(requestId, drinkType, 0, reason);
        }
    }, Qt::QueuedConnection);
}

void NativeRecipeServiceSource::rejectOperation(const QString& requestId,
                                                const QString& operation,
                                                qint64 recipeId,
                                                const QString& reason)
{
    if (requestId.isEmpty())
        return;
    QMetaObject::invokeMethod(this, [this, requestId, operation, recipeId, reason] {
        emit operationCompleted(requestId, operation, false, recipeId, reason, {});
    }, Qt::QueuedConnection);
}

void NativeRecipeServiceSource::onActiveRecipeChanged()
{
    setActiveRecipeId(m_dye ? m_dye->activeRecipeId() : -1);
}

void NativeRecipeServiceSource::completeActivation(qint64 recipeId, bool success,
                                                   const QString& missingProfileTitle)
{
    if (m_activationRequestId.isEmpty() || m_activationRecipeId != recipeId)
        return;
    const QString requestId = m_activationRequestId;
    m_activationRequestId.clear();
    m_activationRecipeId = -1;
    QVariantMap result;
    if (!missingProfileTitle.isEmpty())
        result.insert(QStringLiteral("missingProfileTitle"), missingProfileTitle);
    finishOperation(requestId, QStringLiteral("activate"), success, recipeId,
                    success ? QString() : QStringLiteral("activationFailed"), result);
    if (success)
        refresh();
}
