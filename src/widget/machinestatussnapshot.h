#pragma once

#include <optional>

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QPointer>
#include <QVector>

class DE1Device;
class MachineState;
class QThread;

struct WidgetLastShot {
    double yieldG = 0.0;
    double durationSec = 0.0;
    QString qualityBadge;   // empty → omitted from JSON

    // The validity invariant lives in the type: the only way to get a
    // WidgetLastShot the writer will publish is through make(), which
    // rejects non-finite / negative values (returns nullopt). Keeps the
    // three thin platform readers from having to defend against NaN/Inf.
    static std::optional<WidgetLastShot> make(double yieldG,
                                              double durationSec,
                                              const QString& qualityBadge);
};

// Value type for the machine-status snapshot. One serializer (toJson) is the
// single source of the on-disk schema — the live path and the shutdown
// disconnected path both go through it so they cannot drift.
// Schema: docs/CLAUDE_MD/WIDGET_SNAPSHOT.md
struct WidgetSnapshot {
    bool connected = false;
    QString phase = QStringLiteral("Disconnected");
    std::optional<double> temperatureC;
    std::optional<double> targetTemperatureC;
    std::optional<double> steamTemperatureC;
    std::optional<WidgetLastShot> lastShot;

    QByteArray toJson() const;
};

// Publishes a WidgetSnapshot to platform-shared storage so the iOS WidgetKit /
// Android AppWidget can render machine phase, temperature-vs-target, last-shot
// summary, and an honest connection/staleness state while the app is
// backgrounded or dead. Reads only existing accessors (no new BLE traffic).
// Snapshot assembly runs on the object's thread; the shared-storage write is
// dispatched off the main thread per the project's background-I/O rule.
class MachineStatusSnapshot : public QObject {
    Q_OBJECT

public:
    explicit MachineStatusSnapshot(DE1Device* device,
                                   MachineState* machineState,
                                   QObject* parent = nullptr);
    ~MachineStatusSnapshot() override;

    // Called by the finalized shot-saved wiring with the just-saved espresso
    // shot's summary. Non-finite or negative values are rejected (the single
    // writer-side guard keeps all three platform readers thin). qualityBadge
    // may be empty (then omitted from the snapshot).
    void setLastShot(double yieldG, double durationSec,
                      const QString& qualityBadge = QString());

    // Synchronous publish of a connected=false snapshot. Call on app
    // shutdown / BLE teardown so a dead app degrades honestly. Also latches
    // a shutdown gate so no further async worker is spawned during teardown.
    void publishDisconnected();

private slots:
    void onPhaseChanged();
    void onConnectionChanged();
    void onSampleReceived();

private:
    WidgetSnapshot buildSnapshot() const;
    void flush();                              // assemble + async write
    void writeAsync(const QByteArray& json);   // off-main-thread platform write
    static void platformWrite(const QByteArray& json);

    QPointer<DE1Device> m_device;
    QPointer<MachineState> m_machineState;

    // Event-based coalescing for the ~5 Hz sample stream: only flush when the
    // integer-rounded temperature the widget would display changes. No guard
    // timer — the value change is the trigger. Reset on phase change so a
    // new phase (e.g. Heating→Steaming, different temp source) always flushes.
    std::optional<int> m_lastTempKeyC;

    std::optional<WidgetLastShot> m_lastShot;

    // Outstanding fire-and-forget write workers, tracked so the destructor can
    // join them; m_shuttingDown gates new workers once teardown begins.
    QVector<QThread*> m_workers;
    bool m_shuttingDown = false;

#ifdef DECENZA_TESTING
    friend class tst_MachineStatusSnapshot;
#endif
};
