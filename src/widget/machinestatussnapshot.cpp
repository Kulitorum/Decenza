#include "machinestatussnapshot.h"
#include "widgetsharedkeys.h"
#include "../ble/de1device.h"
#include "../machine/machinestate.h"

#include <climits>

#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QThread>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QCoreApplication>
#elif defined(Q_OS_IOS)
// Implemented in ios/MachineStatusWidgetBridge.mm
extern void decenzaWriteWidgetSnapshotIOS(const QByteArray& json);
#else
#include <QStandardPaths>
#include <QDir>
#include <QSaveFile>
#endif

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
    // immediately rather than showing the disconnected fallback until the
    // first state change.
    flush();
}

void MachineStatusSnapshot::setLastShot(double yieldG, double durationSec,
                                        const QString& qualityBadge)
{
    m_hasLastShot = true;
    m_lastShotYieldG = yieldG;
    m_lastShotDurationSec = durationSec;
    m_lastShotQualityBadge = qualityBadge;
    flush();
}

void MachineStatusSnapshot::onPhaseChanged()
{
    // Phase transitions are always meaningful — flush immediately.
    flush();
}

void MachineStatusSnapshot::onConnectionChanged()
{
    flush();
}

void MachineStatusSnapshot::onSampleReceived()
{
    // ~5 Hz stream. Only flush when the integer-rounded temperature the widget
    // would display actually changes — event/value-based, no guard timer.
    if (!m_device)
        return;
    const bool steaming = m_machineState
        && m_machineState->phase() == MachineState::Phase::Steaming;
    const double t = steaming ? m_device->steamTemperature()
                              : m_device->temperature();
    const int key = qRound(t);
    if (key == m_lastTempKeyC)
        return;
    m_lastTempKeyC = key;
    flush();
}

QByteArray MachineStatusSnapshot::buildSnapshotJson() const
{
    QJsonObject o;
    o["schemaVersion"] = WidgetSharedKeys::kSchemaVersion;

    const bool connected = m_device && m_device->isConnected();
    o["connected"] = connected;
    o["phase"] = m_machineState ? m_machineState->phaseString()
                                : QStringLiteral("Disconnected");
    if (m_device) {
        o["temperatureC"] = m_device->temperature();
        o["targetTemperatureC"] = m_device->goalTemperature();
        o["steamTemperatureC"] = m_device->steamTemperature();
    }
    if (m_hasLastShot) {
        QJsonObject shot;
        shot["yieldG"] = m_lastShotYieldG;
        shot["durationSec"] = m_lastShotDurationSec;
        if (!m_lastShotQualityBadge.isEmpty())
            shot["qualityBadge"] = m_lastShotQualityBadge;
        o["lastShot"] = shot;
    }

    const QDateTime now = QDateTime::currentDateTime();
    o["capturedAt"] = now.toOffsetFromUtc(now.offsetFromUtc())
                          .toString(Qt::ISODate);

    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

void MachineStatusSnapshot::flush()
{
    writeAsync(buildSnapshotJson());
}

void MachineStatusSnapshot::writeAsync(const QByteArray& json)
{
    // Off-main-thread platform write (project background-I/O rule). The
    // payload is a few hundred bytes and flush frequency is low (phase /
    // connection / rounded-temperature changes), so a short-lived worker
    // per flush — the canonical pattern used elsewhere — is appropriate.
    QThread* worker = QThread::create([json]() {
        platformWrite(json);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void MachineStatusSnapshot::publishDisconnected()
{
    // Shutdown path: the event loop is going away, so write synchronously
    // rather than spinning up a worker that may never run.
    QJsonObject o;
    o["schemaVersion"] = WidgetSharedKeys::kSchemaVersion;
    o["connected"] = false;
    o["phase"] = QStringLiteral("Disconnected");
    const QDateTime now = QDateTime::currentDateTime();
    o["capturedAt"] = now.toOffsetFromUtc(now.offsetFromUtc())
                          .toString(Qt::ISODate);
    platformWrite(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void MachineStatusSnapshot::platformWrite(const QByteArray& json)
{
#if defined(Q_OS_ANDROID)
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;
    QJniObject jJson = QJniObject::fromString(QString::fromUtf8(json));
    QJniObject::callStaticMethod<void>(
        "io/github/kulitorum/decenza_de1/MachineStatusWidget",
        "writeSnapshot",
        "(Landroid/content/Context;Ljava/lang/String;)V",
        context.object(), jJson.object<jstring>());
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
    if (f.open(QIODevice::WriteOnly)) {
        f.write(json);
        f.commit();
    }
#endif
}
