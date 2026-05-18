#pragma once

#include <QObject>
#include <QByteArray>
#include <QPointer>

class DE1Device;
class MachineState;

// Publishes a compact machine-status snapshot to platform-shared storage so
// the iOS WidgetKit / Android AppWidget can render machine phase,
// temperature-vs-target, last-shot summary, and an honest connection/staleness
// state while the app is backgrounded or dead.
//
// Reads only existing accessors (no new BLE traffic). Snapshot assembly runs
// on the object's thread; the actual shared-storage write is dispatched off
// the main thread per the project's background-I/O rule.
//
// Schema: openspec/changes/add-machine-status-widget/snapshot-schema.md
class MachineStatusSnapshot : public QObject {
    Q_OBJECT

public:
    explicit MachineStatusSnapshot(DE1Device* device,
                                   MachineState* machineState,
                                   QObject* parent = nullptr);

    // Called by the existing shot-end wiring with the just-finished shot's
    // summary. qualityBadge may be empty (omitted from the snapshot then).
    void setLastShot(double yieldG, double durationSec,
                      const QString& qualityBadge = QString());

    // Synchronous publish of a connected=false snapshot. Call on app
    // shutdown / BLE teardown so a dead app degrades honestly rather than
    // leaving the last "connected" snapshot behind.
    void publishDisconnected();

private slots:
    void onPhaseChanged();
    void onConnectionChanged();
    void onSampleReceived();

private:
    QByteArray buildSnapshotJson() const;
    void flush();                              // assemble + async write
    void writeAsync(const QByteArray& json);   // off-main-thread platform write
    static void platformWrite(const QByteArray& json);

    QPointer<DE1Device> m_device;
    QPointer<MachineState> m_machineState;

    // Event-based coalescing for the ~5 Hz sample stream: only flush when the
    // integer-rounded temperature the widget would display actually changes.
    // No guard timers (project rule) — the value change is the trigger.
    int m_lastTempKeyC = INT_MIN;

    bool m_hasLastShot = false;
    double m_lastShotYieldG = 0.0;
    double m_lastShotDurationSec = 0.0;
    QString m_lastShotQualityBadge;
};
