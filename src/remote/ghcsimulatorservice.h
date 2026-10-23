#pragma once

#include "remoteservice.h"
#include "rep_ghcsimulatorservice_replica.h"

#include "serviceoperation.h"
#include <QMetaObject>
#include <QPointer>
#include <QVariantList>

class QQmlEngine;
class QJSEngine;
class QRemoteObjectNode;

class GHCSimulatorService final : public RemoteService
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY stateChanged FINAL)
    // Match GHCSimulator's state/control contract so the shared window can use
    // one controller API on native and remote clients.
    Q_PROPERTY(QVariantList ledColors READ ledColors NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool stopPressed READ stopPressed NOTIFY stateChanged FINAL)
    Q_PROPERTY(int activeFunction READ activeFunction NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
public:
    ~GHCSimulatorService() override;
    static GHCSimulatorService* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static void setPublishedNode(QRemoteObjectNode* node);
    static QRemoteObjectNode* publishedNode();

    QVariantMap snapshot() const;
    QVariantList ledColors() const;
    bool stopPressed() const;
    int activeFunction() const;
    bool available() const;
    bool ready() const;
    bool busy() const { return !m_pending.isEmpty(); }
    void setNode(QRemoteObjectNode* node) override;

    Q_INVOKABLE QString execute(const QString& operation);
    Q_INVOKABLE void pressEspresso();
    Q_INVOKABLE void pressSteam();
    Q_INVOKABLE void pressHotWater();
    Q_INVOKABLE void pressFlush();
    Q_INVOKABLE void pressStop();
    // Keeps the native controller surface consistent; remote Stop is issued
    // as one terminal command and has no client-held release RPC.
    Q_INVOKABLE void releaseStop();
    Q_INVOKABLE void refresh();
    // Window activation remains local to this QML engine. Names mirror the
    // native GHCSimulator contract, but these signals are not in the .rep API.
    Q_INVOKABLE void mainWindowActivated();
    Q_INVOKABLE void ghcWindowActivated();

signals:
    void stateChanged();
    void busyChanged();
    void operationCompleted(const QString& requestId, const QString& operation,
                            bool success, const QString& reason);
    void raiseMainWindow();
    void raiseGhcWindow();

private:
    friend struct GHCSimulatorServiceForeign;
    explicit GHCSimulatorService(QRemoteObjectNode* node, QObject* parent);
    bool connected() const;
    void changed();
    void complete(const QString& requestId, const QString& operation,
                  bool success, const QString& reason);
    void failPending(const QString& reason);

    QPointer<QRemoteObjectNode> m_node;
    QPointer<GHCSimulatorStateReplica> m_state;
    QMetaObject::Connection m_nodeDestroyed;
    ServiceRequests m_pending;
    bool m_isRaisingWindows = false;
};
