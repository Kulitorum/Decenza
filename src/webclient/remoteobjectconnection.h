#pragma once

#include "remotesessionbackend.h"
#include "remote/remoteservice.h"
#include <QQmlListProperty>
#include <QObject>
#include <QPointer>
#include <QList>
#include <QtQmlIntegration/qqmlintegration.h>

class QRemoteObjectNode;
class QWebSocket;

// A single authenticated connection can carry all published QtRO services.
class RemoteObjectConnection : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(RemoteSessionBackend* backend READ session WRITE setSession NOTIFY sessionChanged FINAL)
    Q_PROPERTY(QQmlListProperty<RemoteService> services READ services NOTIFY servicesChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged FINAL)
public:
    explicit RemoteObjectConnection(QObject* parent = nullptr);
    ~RemoteObjectConnection() override;
    RemoteSessionBackend* session() const { return m_session; }
    QString errorMessage() const { return m_error; }
    void setSession(RemoteSessionBackend* session);
    QQmlListProperty<RemoteService> services();
signals:
    void sessionChanged();
    void servicesChanged();
    void errorMessageChanged();
private:
    static void appendService(QQmlListProperty<RemoteService>*, RemoteService*);
    static qsizetype serviceCount(QQmlListProperty<RemoteService>*);
    static RemoteService* serviceAt(QQmlListProperty<RemoteService>*, qsizetype);
    static void clearServices(QQmlListProperty<RemoteService>*);
    QList<QPointer<RemoteService>> m_services;
    void synchronize();
    void synchronizeServices();
    void close();
    void fail(const QString& message);
    QPointer<RemoteSessionBackend> m_session;
    QRemoteObjectNode* m_node = nullptr;
    QWebSocket* m_socket = nullptr;
    QList<QMetaObject::Connection> m_connections;
    quint64 m_attemptedGeneration = 0;
    bool m_closing = false;
    QString m_error;
};
