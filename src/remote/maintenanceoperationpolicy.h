#pragma once

#include <QString>
#include <QtGlobal>

// Tracks only machine-observed phase transitions. A transport drop between an
// active maintenance phase and a settled phase is not evidence of completion.
class MaintenanceWorkflowCompletion final
{
public:
    void reset()
    {
        m_observedActive = false;
        m_interrupted = false;
        m_needsFinish = false;
        m_stopped = false;
    }

    void end(bool stopped)
    {
        m_observedActive = false;
        m_interrupted = false;
        m_needsFinish = false;
        m_stopped = stopped;
    }

    void observe(bool activePhase, bool disconnected, bool settledPhase)
    {
        if (m_stopped) return;
        if (activePhase) {
            m_observedActive = true;
            m_interrupted = false;
            m_needsFinish = false;
        } else if (disconnected) {
            if (m_observedActive) m_interrupted = true;
            m_needsFinish = false;
        } else if (m_observedActive && !m_interrupted && settledPhase) {
            m_needsFinish = true;
        }
    }

    bool observedActive() const { return m_observedActive; }
    bool interrupted() const { return m_interrupted; }
    bool needsFinish() const { return m_needsFinish; }
    bool stopped() const { return m_stopped; }

private:
    bool m_observedActive = false;
    bool m_interrupted = false;
    bool m_needsFinish = false;
    bool m_stopped = false;
};

class MaintenanceOperationPolicy final
{
public:
    static bool supported(const QString& operation)
    {
        return operation == QLatin1String("beginDescaleHold")
            || operation == QLatin1String("finishDescale")
            || operation == QLatin1String("cancelDescale")
            || operation == QLatin1String("startDescale")
            || operation == QLatin1String("stopDescale")
            || operation == QLatin1String("startTransport")
            || operation == QLatin1String("finishTransport")
            || operation == QLatin1String("stopTransport")
            || operation == QLatin1String("setHeadless");
    }

    static bool startAllowed(bool simulation, bool connected, int phase,
                             int idlePhase, int heatingPhase, int readyPhase)
    {
        return simulation || (connected && (phase == idlePhase || phase == heatingPhase
                                            || phase == readyPhase));
    }

    static bool revisionMatches(qint64 expectedRevision, qint64 currentRevision)
    {
        return expectedRevision >= 0 && expectedRevision == currentRevision;
    }

    static bool hotDescaleAllowed(double steamTemperatureC, bool allowHotSteam)
    {
        return steamTemperatureC <= 60.0 || allowHotSteam;
    }
};
