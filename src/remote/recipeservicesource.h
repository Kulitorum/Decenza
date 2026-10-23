#pragma once

#include "rep_recipeservice_source.h"
#include "serviceoperation.h"

#include <QMetaObject>
#include <QPointer>

#include <functional>

class RecipeStorage;
class SettingsDye;

class NativeRecipeServiceSource final : public RecipeStateSimpleSource
{
    Q_OBJECT
public:
    using ActivateRecipe = std::function<void(qint64)>;
    using DeactivateRecipe = std::function<void()>;

    NativeRecipeServiceSource(RecipeStorage* storage, SettingsDye* dye,
                              ActivateRecipe activateRecipe,
                              DeactivateRecipe deactivateRecipe,
                              QObject* parent = nullptr);

    void refresh() override;
    void requestRecipe(QString requestId, qint64 recipeId) override;
    void requestLastEquipmentForDrinkType(QString requestId, QString drinkType) override;
    void requestCountRecipesUsingProfile(QString requestId, QString profileTitle) override;
    void execute(QString requestId, QString operation, qint64 recipeId,
                 QVariantMap fields, QVariantMap expectedEditable) override;
    void completeActivation(qint64 recipeId, bool success,
                            const QString& missingProfileTitle = QString());

private:
    void finishInventoryRead(quint64 generation, bool archived, bool success,
                             const QVariantList& rows, const QString& reason);
    void finishInventoryBatch(quint64 generation);
    void finishOperation(const QString& requestId, const QString& operation,
                         bool success, qint64 resultId, const QString& reason,
                         const QVariantMap& result = {});
    void rejectRequest(const QString& requestId, const QString& operation,
                       qint64 recipeId, const QString& reason,
                       const QString& drinkType = QString());
    void rejectOperation(const QString& requestId, const QString& operation,
                         qint64 recipeId, const QString& reason);
    void onActiveRecipeChanged();

    QPointer<RecipeStorage> m_storage;
    QPointer<SettingsDye> m_dye;
    ActivateRecipe m_activateRecipe;
    DeactivateRecipe m_deactivateRecipe;
    ServiceRequestIds m_requestIds;
    QString m_activationRequestId;
    qint64 m_activationRecipeId = -1;
    QVariantList m_recipes;
    QVariantList m_archivedRecipes;
    QVariantList m_pendingRecipes;
    QVariantList m_pendingArchivedRecipes;
    quint64 m_inventoryGeneration = 0;
    int m_inventoryReadsRemaining = 0;
    bool m_inventoryReadInFlight = false;
    bool m_inventoryDirty = false;
    bool m_refreshQueued = false;
    bool m_inventoryReadFailed = false;
    QString m_inventoryFailureReason;
};
