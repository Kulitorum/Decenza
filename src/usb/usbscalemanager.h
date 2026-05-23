#pragma once

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

    // Create + open the UsbDecentScale for the currently-available USB scale and
    // emit scaleDiscovered() so main.cpp wires it active. No-op if no scale is
    // available or one is already connected. Called when the user selects the
    // USB entry in the discovered list (via BLEManager::usbConnectRequested) or
    // on startup when the USB scale is the saved primary.
    Q_INVOKABLE void connectToScale();

signals:
    void scaleConnectedChanged();
    void scaleDiscovered(UsbDecentScale* scale);
    void scaleLost();
    // Probe-confirmed presence (NOT a connection): the USB scale is plugged in
    // and answered with a valid weight packet. main.cpp lists it as a selectable
    // entry and auto-connects only when it's the saved primary.
    void usbScaleAvailable();
    void usbScaleUnavailable();
    void logMessage(const QString& message);

private slots:
    void onPollTimerTick();

private:
    QTimer m_pollTimer;
    UsbDecentScale* m_scale = nullptr;
    bool m_hasLoggedInitialPorts = false;
    // True once a scale has been probe-confirmed and is still plugged in (but
    // NOT necessarily connected). Drives usbScaleAvailable/Unavailable.
    bool m_scaleAvailable = false;
    void setScaleAvailable(bool available);

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

    static constexpr int POLL_INTERVAL_MS = 2000;
    static constexpr int PROBE_TIMEOUT_MS = 3000;
    static constexpr uint16_t VENDOR_ID_WCH = 0x1A86;
    static constexpr uint16_t PRODUCT_ID_SCALE_1 = 0x7522;  // CH340 variant
    static constexpr uint16_t PRODUCT_ID_SCALE_2 = 0x7523;  // CH340 variant

    static bool isScalePid(uint16_t pid) { return pid == PRODUCT_ID_SCALE_1 || pid == PRODUCT_ID_SCALE_2; }
};
