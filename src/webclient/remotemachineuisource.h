#pragma once

#include "remotesessionbackend.h"
#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>

// Shared lifetime/freshness handling for the native-named browser facades.
class RemoteMachineUiSource : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(RemoteSessionBackend* session READ session WRITE setSession NOTIFY sessionChanged FINAL)
public:
    explicit RemoteMachineUiSource(QObject* parent = nullptr);
    RemoteSessionBackend* session() const { return m_session.data(); }
    virtual void setSession(RemoteSessionBackend* session);

signals:
    void sessionChanged();
    void machineUiChanged();

protected:
    bool sessionIsFresh() const;
    const MachineUiSnapshot& machineUi() const;

private:
    QPointer<RemoteSessionBackend> m_session;
    QList<QMetaObject::Connection> m_connections;
};
