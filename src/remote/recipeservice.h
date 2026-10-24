#pragma once
#include "remoteservice.h"

#include "rep_recipeservice_replica.h"
#include "serviceoperation.h"
#include <QHash>
#include <QObject>
#include <QPointer>

class QRemoteObjectNode;

// The existing app's recipe storage-shaped UI contract, shared by both kits.
class RecipeService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantList recipes READ recipes NOTIFY recipesChanged FINAL)
    Q_PROPERTY(QVariantList archivedRecipes READ archivedRecipes NOTIFY recipesChanged FINAL)
    Q_PROPERTY(qint64 activeRecipeId READ activeRecipeId NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString operationError READ operationError NOTIFY stateChanged FINAL)
public:
    ~RecipeService() override;
    QVariantList recipes() const;
    QVariantList archivedRecipes() const;
    qint64 activeRecipeId() const;
    bool ready() const;
    bool loading() const;
    bool busy() const { return m_operation.busy(); }
    QString errorMessage() const;
    QString operationError() const { return m_operation.error(); }
    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE QVariantMap editableSnapshot(const QVariantMap& recipe) const;
    Q_INVOKABLE bool isSaveValid(const QString& name, const QString& profileTitle,
                                 const QString& hotWaterJson) const;
    Q_INVOKABLE void requestInventory();
    Q_INVOKABLE void requestArchived();
    Q_INVOKABLE void requestRecipe(qint64 id);
    Q_INVOKABLE void requestLastEquipmentForDrinkType(const QString& drinkType);
    Q_INVOKABLE QString requestCountRecipesUsingProfile(const QString& profileTitle);
    Q_INVOKABLE void requestCreateRecipe(const QVariantMap& fields);
    Q_INVOKABLE QString requestUpdateRecipe(qint64 id, const QVariantMap& fields,
                                         const QVariantMap& expectedEditable = {});
    Q_INVOKABLE void requestCloneRecipe(qint64 id, const QString& name, const QString& token = {});
    Q_INVOKABLE void requestArchiveRecipe(qint64 id);
    Q_INVOKABLE void requestUnarchiveRecipe(qint64 id);
    Q_INVOKABLE void requestDeleteRecipe(qint64 id);
    Q_INVOKABLE void requestRelinkRecipeToBag(qint64 id, qint64 bagId);
    Q_INVOKABLE QString requestActivateRecipe(qint64 id);
    Q_INVOKABLE void requestDeactivateRecipe();
signals:
    void stateChanged();
    void recipesChanged();
    void busyChanged();
    void inventoryReady(const QVariantList& recipes);
    void archivedReady(const QVariantList& recipes);
    void inventoryFailed(const QString& reason);
    void recipeReady(qint64 id, const QVariantMap& recipe);
    void recipeCheckFailed(qint64 id);
    void lastEquipmentForDrinkTypeReady(const QString& drinkType, qint64 equipmentId);
    void lastEquipmentForDrinkTypeFailed(const QString& drinkType);
    void countRecipesUsingProfileReady(const QString& requestId, const QString& profileTitle,
                                       int count, const QString& reason);
    void recipeCreated(qint64 id, const QVariantMap& recipe);
    void recipeUpdateFailed(qint64 id, const QString& reason);
    void recipeUpdated(qint64 id, bool success);
    void recipeDeleted(qint64 id, bool success);
    void recipeActivated(qint64 id, bool success);
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, qint64 resultId, const QString& reason);
private:
    friend struct RecipeServiceForeign;
    explicit RecipeService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void changed();
    QVariantMap snapshotFor(qint64 id) const;
    void failPending();
    void send(const QString& operation, qint64 id, const QVariantMap& fields = {},
              const QVariantMap& expected = {}, const QString& uiId = {});
    void report(const QString& uiId, const QString& operation, qint64 originalId,
                bool success, qint64 resultId, const QString& reason, const QVariantMap& result);
    QPointer<QRemoteObjectNode> m_node;
    QPointer<RecipeStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    ServiceOperation m_operation;
    bool m_refreshQueued = false;
    QHash<QString, qint64> m_reads;
    QHash<QString, QString> m_equipmentReads;
    QHash<QString, QString> m_profileCountReads;
    QVariantList m_notifiedRecipes;
    QVariantList m_notifiedArchived;
};
