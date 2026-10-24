#include "remotemachineuisource.h"
#include <utility>

RemoteMachineUiSource::RemoteMachineUiSource(QObject* parent) : QObject(parent) {}

void RemoteMachineUiSource::setSession(RemoteSessionBackend* session)
{
    if (m_session == session)
        return;
    for (const auto& connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_session = session;
    if (session) {
        m_connections.append(connect(session, &RemoteSessionBackend::snapshotChanged,
                                     this, &RemoteMachineUiSource::machineUiChanged));
        m_connections.append(connect(session, &RemoteSessionBackend::connectedChanged,
                                     this, &RemoteMachineUiSource::machineUiChanged));
        m_connections.append(connect(session, &RemoteSessionBackend::staleChanged,
                                     this, &RemoteMachineUiSource::machineUiChanged));
        m_connections.append(connect(session, &QObject::destroyed, this, [this] {
            m_session = nullptr;
            emit sessionChanged();
            emit machineUiChanged();
        }));
    }
    emit sessionChanged();
    emit machineUiChanged();
}

bool RemoteMachineUiSource::sessionIsFresh() const
{
    return m_session && m_session->connected() && !m_session->stale();
}

const MachineUiSnapshot& RemoteMachineUiSource::machineUi() const
{
    static const MachineUiSnapshot disconnected;
    return sessionIsFresh()
        ? m_session->machineUi() : disconnected;
}
