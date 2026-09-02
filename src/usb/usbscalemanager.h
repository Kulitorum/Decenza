#pragma once

#include "usbhotplug.h"

#include <QObject>
#include <QSet>
#include <QTimer>

#ifndef Q_OS_ANDROID
#include <QSerialPort>
#include <QSerialPortInfo>
#endif

class UsbDecentScale;

/**
 * USB scale discovery manager for Half Decent Scale.
 *
 * Polls for a USB scale by VID 0x1A86 + PID 0x7523 (CH340).
 * On Android: uses JNI (AndroidUsbScaleHelper).
 * On desktop: uses QSerialPortInfo.
 *
 * When the scale is confirmed (receives valid weight packets), it is recorded
 * as AVAILABLE and emits usbScaleAvailable() — it does NOT auto-connect, so it
 * can be tested over Bluetooth/WiFi instead. The discovered-devices list shows
 * it as a selectable entry; selecting it (or auto-reconnect when it's the saved
 * primary) calls connectToScale(), which creates a UsbDecentScale, opens it, and
 * emits scaleDiscovered() to wire it active.
 * When unplugged, emits usbScaleUnavailable() (and scaleLost() if connected).
 */
class UsbScaleManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool scaleConnected READ isScaleConnected NOTIFY scaleConnectedChanged)

public:
    explicit UsbScaleManager(QObject* parent = nullptr);
    ~UsbScaleManager() override;

    bool isScaleConnected() const;
    bool isScaleAvailable() const { return m_scaleAvailable; }
    UsbDecentScale* scale() const { return m_scale; }

    void startPolling();
    void stopPolling();

    // Platform-reported attach/detach. Runs the timer's own pass, which is
    // hasDevice()-driven, so attach and detach need no separate handling.
    //
    // Not probeNow(): that carries user-scan semantics (forgets probed ports, emits
    // probeFinished for the scanning indicator), which a cable event is not.
    void onHotplugEvent();

    // Run a poll pass NOW instead of waiting up to POLL_INTERVAL_MS for the next
    // tick, and report completion via probeFinished().
    //
    // Exists because "Scan for Devices" means find my devices, not find my
    // Bluetooth devices. USB detection is otherwise a free-running background
    // poll that the scan button does not touch, so a scale plugged in moments
    // before a scan appeared only when the timer happened to come round — and
    // the scan indicator never accounted for USB at all.
    void probeNow();

    // Create + open the UsbDecentScale for the currently-available USB scale and
    // emit scaleDiscovered() so main.cpp wires it active. No-op if no scale is
    // available or one is already connected. Called when the user selects the
    // USB entry in the discovered list (via BLEManager::usbConnectRequested) or
    // on startup when the USB scale is the saved primary.
    Q_INVOKABLE void connectToScale();

    // Tear down an active USB scale connection WITHOUT marking it lost. Used when
    // the user switches to a BLE/WiFi scale: the USB scale is still plugged in
    // (m_scaleAvailable stays true, no scaleLost()), we just stop feeding its
    // weight so the new scale doesn't double-feed WeightProcessor. No-op if no
    // scale is connected.
    void disconnectScale();

signals:
    void scaleConnectedChanged();
    void scaleDiscovered(UsbDecentScale* scale);
    void scaleLost();
    // Probe-confirmed presence (NOT a connection): the USB scale is plugged in
    // and answered with a valid weight packet. main.cpp lists it as a selectable
    // entry and auto-connects only when it's the saved primary.
    void usbScaleAvailable();
    void usbScaleUnavailable();
    // A probeNow() pass has finished. Used by the scan to know when the USB
    // third of "Scanning..." is done.
    void probeFinished();
    void logMessage(const QString& message);

private slots:
    void onPollTimerTick();

private:
    // The ONLY way this class writes a log line. Each does the [Scale][USB Scale]
    // prefix, the stderr write at the right severity, and the logMessage emit that
    // feeds the in-app scale log and its shareable export — so the prefix and the
    // wording exist once per event instead of once per output.
    //
    // Every site used to hand-roll all of that: 73 inline "[USB Scale] " prefixes,
    // and at 21 of them the qDebug and the logMessage had drifted into describing
    // the same event in different words. Do not reintroduce a bare qDebug or a bare
    // emit here; use these. (Free functions in the .cpp cannot be used instead —
    // the emit needs the instance.)
    void log(const QString& message);   // DEBUG — probe/poll detail
    void info(const QString& message);  // INFO  — the user-facing narrative
    void warn(const QString& message);  // WARN  — problems

    // Emits probeFinished() exactly once per scan-initiated pass, from the point
    // the probe actually settles rather than the point it was started.
    void finishScanProbe();
    bool m_scanProbePending = false;

    QTimer m_pollTimer;
    UsbDecentScale* m_scale = nullptr;
    bool m_hasLoggedInitialPorts = false;
    // True once a scale has been probe-confirmed and is still plugged in (but
    // NOT necessarily connected). Drives usbScaleAvailable/Unavailable.
    bool m_scaleAvailable = false;
    void setScaleAvailable(bool available);

    // Shared teardown for a connected scale that has gone away (poll-detected
    // unplug OR connectedChanged → disconnected). Emits scaleLost() while
    // m_scale is still valid (main.cpp's handler reads scale() to unwire weight
    // signals), THEN deletes + nulls it, releases the platform connection, and
    // marks the scale unavailable. Idempotent: no-op when m_scale is already
    // null, so the poll detector and the connectedChanged handler can't
    // double-fire. Returns true if it actually tore a scale down.
    bool teardownConnectedScale();

#ifdef Q_OS_ANDROID
    void onPollTimerTickAndroid();
    void probeAndroid();
    void onAndroidProbeRead();
    void onAndroidProbeTimeout();
    void cleanupAndroidProbe(bool closeConnection);

    bool m_androidProbing = false;
    bool m_androidPermissionRequested = false;
    QByteArray m_probeBuffer;
    QTimer* m_androidProbeTimer = nullptr;
    QTimer* m_androidReadTimer = nullptr;
#else
    void onPollTimerTickDesktop();
    void probePort(const QSerialPortInfo& portInfo);
    void onProbeReadyRead();
    void onProbeTimeout();
    void cleanupProbe();

    QSet<QString> m_knownPorts;
    QSerialPort* m_probePort = nullptr;
    QTimer* m_probeTimer = nullptr;
    QSerialPortInfo m_probingPortInfo;
    QByteArray m_probeBuffer;
    // Port name confirmed by the last successful probe; reopened by
    // connectToScale() when the user selects the USB entry. Cleared on unplug.
    QString m_confirmedPortName;
#endif

    // Android has hotplug, so the timer only catches a broadcast that never
    // arrived (backgrounded, or an OEM build that drops it) — a safety net, hence
    // slow. Elsewhere there is no hotplug (see usbhotplug.h) and the timer IS the
    // detection path.
#ifdef Q_OS_ANDROID
    static constexpr int POLL_INTERVAL_MS = 60000;   // hotplug fallback
#else
    static constexpr int POLL_INTERVAL_MS = 2000;    // sole detection path
#endif
    static constexpr int PROBE_TIMEOUT_MS = 3000;
    static constexpr uint16_t VENDOR_ID_WCH = 0x1A86;
    // Ids live in usbhotplug.h — one declaration, since its classifier treats
    // anything unlisted as the DE1.
    static bool isScalePid(uint16_t pid)
    {
        return usbDeviceKindForPid(static_cast<int>(pid)) == UsbDeviceKind::Scale;
    }
};
