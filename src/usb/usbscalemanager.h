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
 * When the scale is confirmed (receives valid weight packets),
 * creates a UsbDecentScale and emits scaleDiscovered().
 * When unplugged, emits scaleLost().
 */
class UsbScaleManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool scaleConnected READ isScaleConnected NOTIFY scaleConnectedChanged)

public:
    explicit UsbScaleManager(QObject* parent = nullptr);
    ~UsbScaleManager() override;

    bool isScaleConnected() const;
    UsbDecentScale* scale() const { return m_scale; }

    void startPolling();
    void stopPolling();

signals:
    void scaleConnectedChanged();
    void scaleDiscovered(UsbDecentScale* scale);
    void scaleLost();
    void logMessage(const QString& message);

private slots:
    void pollPorts();

private:
    QTimer m_pollTimer;
    UsbDecentScale* m_scale = nullptr;
    bool m_hasLoggedInitialPorts = false;

#ifdef Q_OS_ANDROID
    void pollPortsAndroid();
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
    void pollPortsDesktop();
    void probePort(const QSerialPortInfo& portInfo);
    void onProbeReadyRead();
    void onProbeTimeout();
    void cleanupProbe();

    QSet<QString> m_knownPorts;
    QSerialPort* m_probePort = nullptr;
    QTimer* m_probeTimer = nullptr;
    QSerialPortInfo m_probingPortInfo;
    QByteArray m_probeBuffer;
#endif

    static constexpr int POLL_INTERVAL_MS = 2000;
    static constexpr int PROBE_TIMEOUT_MS = 3000;
    static constexpr uint16_t VENDOR_ID_WCH = 0x1A86;
    static constexpr uint16_t PRODUCT_ID_SCALE_1 = 0x7522;  // CH340 variant
    static constexpr uint16_t PRODUCT_ID_SCALE_2 = 0x7523;  // CH340 variant

    static bool isScalePid(uint16_t pid) { return pid == PRODUCT_ID_SCALE_1 || pid == PRODUCT_ID_SCALE_2; }
};
