#pragma once

#include "remoteservice.h"
#include "rep_screensaverservice_replica.h"

#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <QtQmlIntegration/qqmlintegration.h>

class QJSEngine;
class QQmlEngine;
class QRemoteObjectNode;
class ScreensaverVideoManager;
class MemoryMonitor;

class ScreensaverService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(ScreensaverStateReplica* state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool isRemoteClient READ isRemoteClient CONSTANT FINAL)
    Q_PROPERTY(bool hasHardwareVideoDecoder READ hasHardwareVideoDecoder CONSTANT FINAL)

public:
    ~ScreensaverService() override;
    ScreensaverStateReplica* state() const { return m_state; }
    bool ready() const;
    bool busy() const { return !m_pending.isEmpty(); }
    QString errorMessage() const { return m_errorMessage; }
    bool isRemoteClient() const;
    bool hasHardwareVideoDecoder() const;

    void setNode(QRemoteObjectNode* node) override;
    static void setPublishedNode(QRemoteObjectNode* node);
    static void setLocalManager(ScreensaverVideoManager* manager);
    static void setLocalMemoryMonitor(MemoryMonitor* monitor);
    static void setLocalWakeCallbacks(std::function<void()> wakeMachineAndScale,
                                     std::function<void()> wakeScale);
    Q_INVOKABLE bool instrumentedBuild() const;
    Q_INVOKABLE double liveRssMB() const;
    Q_INVOKABLE void wakeMachineAndScale();
    Q_INVOKABLE void wakeScale();

    Q_INVOKABLE QString newRequestId() const;
    Q_INVOKABLE QString nextMedia();
    Q_INVOKABLE QString setScreensaverType(const QString& type);
    Q_INVOKABLE QString refreshCategories();
    Q_INVOKABLE QString refreshCatalog();
    Q_INVOKABLE QString clearVideoCache();
    Q_INVOKABLE QString clearPersonalMedia();
    Q_INVOKABLE QString refreshShotMap();
    Q_INVOKABLE QString importMedia(const QString& transferId);
    Q_INVOKABLE QString cancelMediaImport(const QString& requestId);
    Q_INVOKABLE QStringList overlayChipsForType(const QString& type) const;
    Q_INVOKABLE QString markPlayed(const QString& ticket);
    Q_INVOKABLE QString markCorrupt(const QString& ticket);
    Q_INVOKABLE QString mediaSource(const QString& ticket) const;
    Q_INVOKABLE QUrl backgroundImageUrl(const QString& opaqueImageId) const;
    Q_INVOKABLE void setScreenDimming(int dimPercent);
    Q_INVOKABLE void restoreScreenBrightness();
    Q_INVOKABLE void setKeepScreenOn(bool on);

signals:
    void stateChanged();
    void busyChanged();
    void completed(const QString& requestId, const QString& operation, bool success,
                   const QVariantMap& result, const QString& reason);
    void mediaAvailable();

private:
    friend struct ScreensaverServiceForeign;
    explicit ScreensaverService(QRemoteObjectNode* node, QObject* parent = nullptr);
    static QRemoteObjectNode* publishedNode();
    static ScreensaverVideoManager* localManager();
    QString execute(const QString& operation, const QString& ticket = {});
    void finish(const QString& requestId, const QString& operation, bool success,
                const QVariantMap& result, const QString& reason);
    void failPending(const QString& reason);
    void onStateChanged();

    QPointer<QRemoteObjectNode> m_node;
    QPointer<ScreensaverStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    QHash<QString, QString> m_pending;
    QString m_errorMessage;
};
