#pragma once

#include "rep_recipecontextservice_replica.h"
#include "remoteservice.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRemoteObjectNode>
#include <type_traits>

class QQmlEngine;
class QJSEngine;

class RecipeContextService final : public RemoteService
{
    Q_OBJECT

    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged FINAL)
    Q_PROPERTY(quint64 revision READ revision NOTIFY preferencesChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool historyReady READ historyReady NOTIFY stateChanged FINAL)

public:
    static RecipeContextService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static void setPublishedNode(QRemoteObjectNode* node);

    QVariantMap preferences() const;
    quint64 revision() const;
    bool ready() const;
    bool historyReady() const;
    void setNode(QRemoteObjectNode* node) override;

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setPreference(const QString& requestId, const QString& name,
                                   const QVariant& value, quint64 expectedRevision);
    Q_INVOKABLE void executePresetEdit(const QString& requestId, const QString& domain,
                                       const QString& operation, const QVariantMap& fields,
                                       quint64 expectedRevision);
    Q_INVOKABLE void applyShotBaseline(const QString& requestId, const QVariantMap& fields,
                                       quint64 expectedRevision);
    Q_INVOKABLE void requestHistory(const QString& requestId, const QString& operation,
                                    const QVariantMap& fields = {});
    Q_INVOKABLE void requestShotMetadataUpdate(const QString& requestId, qint64 shotId,
                                              const QVariantMap& metadata);

signals:
    void preferencesChanged();
    void stateChanged();
    void preferenceCompleted(const QString& requestId, const QString& name,
                             bool success, const QVariantMap& preferences,
                             const QString& reason);
    void presetEditCompleted(const QString& requestId, const QString& domain,
                             const QString& operation, bool success, quint64 revision,
                             const QVariantMap& preferences, const QString& reason);
    void shotBaselineCompleted(const QString& requestId, bool success,
                               quint64 revision, const QVariantMap& preferences, const QString& reason);
    void historyCompleted(const QString& requestId, const QString& operation,
                          bool success, const QVariantMap& result,
                          const QString& reason);

private:
    explicit RecipeContextService(QRemoteObjectNode* node, QObject* parent);
    bool connected() const;
    void failPending(const QString& reason);
    void failPreferencePending(const QString& reason);
    void failPresetEditPending(const QString& reason);
    void finishHistory(const QString& requestId, const QString& operation,
                       bool success, const QVariantMap& result, const QString& reason);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<RecipeContextStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pendingPreferences;
    QHash<QString, QPair<QString, QString>> m_pendingPresetEdits;
    QHash<QString, QString> m_pendingHistory;
};

static_assert(!std::is_default_constructible_v<RecipeContextService>,
              "The QML singleton must use its engine-owned factory.");
