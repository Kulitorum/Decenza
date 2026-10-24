#pragma once

#include "remoteservice.h"
#include "rep_layoutservice_replica.h"
#include "serviceoperation.h"

#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QVariantMap>

class QRemoteObjectNode;

// The single layout facade used by native and WebAssembly QML. Reads come
// from one portable snapshot view; writes are correlated host operations.
class LayoutService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QString layoutConfiguration READ layoutConfiguration NOTIFY stateChanged FINAL)
    Q_PROPERTY(qint64 revision READ revision NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(qsizetype pendingCount READ pendingCount NOTIFY stateChanged FINAL)
public:
    ~LayoutService() override;
    QString layoutConfiguration() const { return m_configuration; }
    qint64 revision() const { return m_revision; }
    bool ready() const;
    QString errorMessage() const;
    qsizetype pendingCount() const { return m_pending.size(); }

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);

    Q_INVOKABLE QString newRequestId() const { return ServiceOperation::newId(); }
    // Pass a revision captured when an editor opened to avoid applying a stale
    // multi-field draft over another client's intervening layout change. The
    // default captures the current revision for immediate one-shot actions.
    Q_INVOKABLE QString execute(const QString& operation, const QVariantMap& fields,
                                qint64 expectedRevision = -1);

    Q_INVOKABLE QVariantList getZoneItems(const QString& zoneName) const;
    Q_INVOKABLE int getZoneYOffset(const QString& zoneName) const;
    Q_INVOKABLE double getZoneScale(const QString& zoneName) const;
    Q_INVOKABLE QString getZoneOption(const QString& zoneName, const QString& key,
                                      const QString& defaultValue) const;
    Q_INVOKABLE QVariantMap getItemProperties(const QString& itemId) const;
    Q_INVOKABLE bool itemIsConfigured(const QString& itemId) const;

    // Layout catalogs are already pure/shared and remain available through the
    // same facade, so shared layout QML has no target-specific import path.
    Q_INVOKABLE static bool typeHasOptions(const QString& type);
    Q_INVOKABLE static QStringList optionKeysForType(const QString& type);
    Q_INVOKABLE static QStringList zoneOptionValues(const QString& key);
    Q_INVOKABLE static QJsonObject readoutCapabilitiesJson();
    Q_INVOKABLE static QString defaultDisplayModeForType(const QString& type);
    Q_INVOKABLE static QJsonObject displayModeDefaultsJson();
    Q_INVOKABLE static QVariantMap sleepOptionDefaults();
    Q_INVOKABLE static QJsonObject sleepOptionDefaultsJson();
    Q_INVOKABLE static QVariantList widgetCatalog();
    Q_INVOKABLE static QVariantMap widgetChipNames();
    Q_INVOKABLE static QVariantList widgetCategoryNames();
    Q_INVOKABLE static QJsonObject widgetCatalogJson();
    Q_INVOKABLE static QVariantList layoutActionCatalog();
    Q_INVOKABLE static QVariantMap layoutActionLabels();
    Q_INVOKABLE static QJsonObject layoutActionCatalogJson();
    Q_INVOKABLE static QString gestureReservedActionForType(const QString& type);
    Q_INVOKABLE static bool typeSupportsGestureOverrides(const QString& type);

signals:
    void stateChanged();
    void layoutConfigurationChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, qint64 revision,
                            const QString& layoutConfiguration, const QString& reason);

private:
    friend struct LayoutServiceForeign;
    explicit LayoutService(QRemoteObjectNode* node, QObject* parent);
    static QRemoteObjectNode* publishedNode();
    bool connected() const;
    void updateFromReplica();
    void failPending(const QString& reason);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<LayoutStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QString m_configuration;
    QString m_error;
    qint64 m_revision = -1;
    bool m_ready = false;
};
