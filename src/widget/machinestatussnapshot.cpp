#include "machinestatussnapshot.h"
#include "widgetsharedkeys.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"

#include <atomic>
#include <cmath>

#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QThread>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#elif defined(Q_OS_IOS)
// Implemented in ios/MachineStatusWidgetBridge.mm
extern void decenzaWriteWidgetSnapshotIOS(const QByteArray& json);
#else
#include <QStandardPaths>
#include <QDir>
#include <QSaveFile>
#endif

std::optional<WidgetLastShot> WidgetLastShot::make(double yieldG,
                                                   double durationSec,
                                                   const QString& qualityBadge)
{
    if (!std::isfinite(yieldG) || !std::isfinite(durationSec)
        || yieldG < 0.0 || durationSec < 0.0)
        return std::nullopt;
    return WidgetLastShot{ yieldG, durationSec, qualityBadge };
}

// --- WidgetSnapshot: the single serializer -----------------------------------

QByteArray WidgetSnapshot::toJson() const
{
    QJsonObject o;
    o["schemaVersion"] = WidgetSharedKeys::kSchemaVersion;
    o["connected"] = connected;
    o["phase"] = phase;
    if (temperatureC)        o["temperatureC"] = *temperatureC;
    if (targetTemperatureC)  o["targetTemperatureC"] = *targetTemperatureC;
    if (steamTemperatureC)   o["steamTemperatureC"] = *steamTemperatureC;
    if (lastShot) {
        QJsonObject shot;
        shot["yieldG"] = lastShot->yieldG;
        shot["durationSec"] = lastShot->durationSec;
        if (!lastShot->qualityBadge.isEmpty())
            shot["qualityBadge"] = lastShot->qualityBadge;
        o["lastShot"] = shot;
    }
    const QDateTime now = QDateTime::currentDateTime();
    o["capturedAt"] = now.toOffsetFromUtc(now.offsetFromUtc())
                          .toString(Qt::ISODate);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// --- MachineStatusSnapshot ---------------------------------------------------

MachineStatusSnapshot::MachineStatusSnapshot(DE1Device* device,
                                             MachineState* machineState,
                                             QObject* parent)
    : QObject(parent)
    , m_device(device)
    , m_machineState(machineState)
{
    if (m_machineState) {
        connect(m_machineState, &MachineState::phaseChanged,
                this, &MachineStatusSnapshot::onPhaseChanged);
    }
    if (m_device) {
        connect(m_device, &DE1Device::connectedChanged,
                this, &MachineStatusSnapshot::onConnectionChanged);
        connect(m_device, &DE1Device::shotSampleReceived,
                this, &MachineStatusSnapshot::onSampleReceived);
    }
    // Publish an initial snapshot so the widget has something to render
    // immediately rather than the disconnected fallback until the first
    // state change.
    flush();
}

MachineStatusSnapshot::~MachineStatusSnapshot()
{
    m_shuttingDown = true;
    for (QThread* w : std::as_const(m_workers)) {
        if (w) {
            w->wait();
            delete w;
        }
    }
}

void MachineStatusSnapshot::setLastShot(double yieldG, double durationSec,
                                        const QString& qualityBadge)
{
    auto shot = WidgetLastShot::make(yieldG, durationSec, qualityBadge);
    if (!shot) {
        // This setter is wired to the finalized shotSaved path, so an
        // invalid value here means that contract was violated (e.g. a
        // ShotDataModel sentinel like stopTime == -1) — surface it once
        // instead of silently never updating the widget's last-shot line.
        static bool warned = false;
        if (!warned) {
            warned = true;
            qWarning() << "[widget] setLastShot rejected non-finalized shot:"
                       << "yieldG" << yieldG << "durationSec" << durationSec;
        }
        return;
    }
    m_lastShot = shot;
    flush();
}

void MachineStatusSnapshot::onPhaseChanged()
{
    // Phase transitions are always meaningful, and the temperature source can
    // change with phase (group vs steam) — reset the coalescing key so the
    // first sample of the new phase always flushes.
    m_lastTempKeyC.reset();
    flush();
}

void MachineStatusSnapshot::onConnectionChanged()
{
    flush();
}

void MachineStatusSnapshot::onSampleReceived()
{
    if (!m_device)
        return;
    const bool steaming = m_machineState
        && m_machineState->phase() == MachineState::Phase::Steaming;
    const double t = steaming ? m_device->steamTemperature()
                              : m_device->temperature();
    const int key = qRound(t);
    if (m_lastTempKeyC && *m_lastTempKeyC == key)
        return;
    m_lastTempKeyC = key;
    flush();
}

WidgetSnapshot MachineStatusSnapshot::buildSnapshot() const
{
    WidgetSnapshot s;
    s.connected = m_device && m_device->isConnected();
    if (m_machineState)
        s.phase = m_machineState->phaseString();
    // Only carry temperatures when actually connected — readers gate on
    // `connected` anyway, so emitting them while disconnected would just be
    // dishonest, self-contradicting JSON.
    if (s.connected) {
        s.temperatureC = m_device->temperature();
        s.targetTemperatureC = m_device->goalTemperature();
        s.steamTemperatureC = m_device->steamTemperature();
    }
    s.lastShot = m_lastShot;
    return s;
}

void MachineStatusSnapshot::flush()
{
    writeAsync(buildSnapshot().toJson());
}

void MachineStatusSnapshot::writeAsync(const QByteArray& json)
{
    if (m_shuttingDown)
        return;
    // Off-main-thread platform write. Payload is a few hundred bytes and
    // flush frequency is low (phase/connection/rounded-temperature changes),
    // so a short-lived fire-and-forget worker per flush is appropriate (no
    // result to marshal back — the simpler half of the project's
    // background-I/O pattern).
    QThread* worker = QThread::create([json]() { platformWrite(json); });
    m_workers.append(worker);
    connect(worker, &QThread::finished, this, [this, worker]() {
        m_workers.removeOne(worker);
        worker->deleteLater();
    });
    worker->start();
}

void MachineStatusSnapshot::publishDisconnected()
{
    // Shutdown path: latch the gate so connectedChanged firing later in the
    // same aboutToQuit teardown can't spawn a worker that outlives us, then
    // write synchronously (the event loop is going away).
    m_shuttingDown = true;
    // Join any in-flight workers first so a flush that started just before
    // shutdown can't commit AFTER this disconnected write and leave a stale
    // connected=true snapshot on disk.
    for (QThread* w : std::as_const(m_workers)) {
        if (w)
            w->wait();
    }
    WidgetSnapshot s;          // connected=false, phase="Disconnected"
    platformWrite(s.toJson());
}

void MachineStatusSnapshot::platformWrite(const QByteArray& json)
{
    // Best-effort, but never silently-forever: a persistent failure here
    // makes the widget show "Disconnected" while the machine is live, so it
    // must leave one searchable breadcrumb (throttled to avoid flush-rate
    // spam).
    // platformWrite runs on a fresh worker thread per flush; the throttle
    // counter must be atomic. Snapshot once via fetch_add so the gate and
    // the printed occurrence number are consistent.
    static std::atomic<int> failCount{0};
    auto logFail = [](const char* what) {
        const int n = failCount.fetch_add(1);
        if (n == 0 || n % 100 == 0)
            qWarning().noquote()
                << "[widget] snapshot write failed:" << what
                << "(occurrence" << (n + 1) << ")";
    };

#if defined(Q_OS_ANDROID)
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        logFail("Android context invalid");
        return;
    }
    QJniObject jJson = QJniObject::fromString(QString::fromUtf8(json));
    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/MachineStatusWidget",
        "writeSnapshot",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        context.object(), jJson.object<jstring>());
    QJniEnvironment env;
    if (env.checkAndClearExceptions())
        logFail("JNI exception in MachineStatusWidget.writeSnapshot "
                "(Java/C++ schema drift?)");
#elif defined(Q_OS_IOS)
    decenzaWriteWidgetSnapshotIOS(json);
#else
    const QString dir = QStandardPaths::writableLocation(
                            QStandardPaths::AppDataLocation)
                        + QDir::separator()
                        + QString::fromUtf8(WidgetSharedKeys::kDesktopSubdir);
    QDir().mkpath(dir);
    QSaveFile f(dir + QDir::separator()
                + QString::fromUtf8(WidgetSharedKeys::kDesktopFileName));
    if (!f.open(QIODevice::WriteOnly)) {
        logFail(qUtf8Printable("open " + f.fileName() + ": "
                               + f.errorString()));
        return;
    }
    f.write(json);
    if (!f.commit())
        logFail(qUtf8Printable("commit: " + f.errorString()));
#endif
}
