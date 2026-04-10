#pragma once

#include <QObject>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothLocalDevice>
#include <QList>
#include <QVariant>
#include <QPermissions>
#include <QTimer>
#include <QStringList>
#include <QFile>

class ScaleDevice;
class DiFluidR2;

// Helper to get device identifier - iOS uses UUID, others use MAC address
inline QString getDeviceIdentifier(const QBluetoothDeviceInfo& device) {
#ifdef Q_OS_IOS
    // iOS doesn't expose MAC addresses, use UUID instead
    return device.deviceUuid().toString();
#else
    return device.address().toString();
#endif
}

// Helper to compare device identifiers
inline bool deviceIdentifiersMatch(const QBluetoothDeviceInfo& device, const QString& identifier) {
#ifdef Q_OS_IOS
    return device.deviceUuid().toString() == identifier;
#else
    return device.address().toString().compare(identifier, Qt::CaseInsensitive) == 0;
#endif
}

class BLEManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)
    Q_PROPERTY(bool bluetoothAvailable READ isBluetoothAvailable NOTIFY bluetoothAvailableChanged)
    Q_PROPERTY(QVariantList discoveredDevices READ discoveredDevices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList discoveredScales READ discoveredScales NOTIFY scalesChanged)
    Q_PROPERTY(bool scaleConnectionFailed READ scaleConnectionFailed NOTIFY scaleConnectionFailedChanged)
    Q_PROPERTY(QVariantList discoveredRefractometers READ discoveredRefractometers NOTIFY refractometersChanged)
    Q_PROPERTY(bool refractometerConnected READ isRefractometerConnected NOTIFY refractometerConnectedChanged)
    Q_PROPERTY(bool hasSavedDE1 READ hasSavedDE1 CONSTANT)
    Q_PROPERTY(bool disabled READ isDisabled WRITE setDisabled NOTIFY disabledChanged)

public:
    explicit BLEManager(QObject* parent = nullptr);
    ~BLEManager();

    bool isScanning() const;
    bool isBluetoothAvailable() const;
    bool isScanningForScales() const { return m_scanningForScales; }
    bool isDisabled() const { return m_disabled; }
    void setDisabled(bool disabled);  // Disable all BLE operations (for simulator mode)
    QVariantList discoveredDevices() const;
    QVariantList discoveredScales() const;
    bool scaleConnectionFailed() const { return m_scaleConnectionFailed; }
    bool hasSavedScale() const { return !m_savedScaleAddress.isEmpty(); }
    bool hasSavedDE1() const { return !m_savedDE1Address.isEmpty(); }

    Q_INVOKABLE QBluetoothDeviceInfo getScaleDeviceInfo(const QString& address) const;
    Q_INVOKABLE QString getScaleType(const QString& address) const;
    Q_INVOKABLE void connectToScale(const QString& address);  // Manual scale selection

    ScaleDevice* scaleDevice() const { return m_scaleDevice; }
    void setScaleDevice(ScaleDevice* scale);

    // Scale address management
    Q_INVOKABLE void setSavedScaleAddress(const QString& address, const QString& type, const QString& name);
    Q_INVOKABLE void clearSavedScale();

    // Refractometer support
    QVariantList discoveredRefractometers() const;
    bool isRefractometerConnected() const;
    QBluetoothDeviceInfo getRefractometerDeviceInfo(const QString& address) const;
    Q_INVOKABLE void connectToRefractometer(const QString& address);
    Q_INVOKABLE void setSavedRefractometerAddress(const QString& address, const QString& name);
    Q_INVOKABLE void clearSavedRefractometer();
    void setRefractometerDevice(DiFluidR2* device);
    Q_INVOKABLE void tryDirectConnectToRefractometer();

    // DE1 address management
    void setSavedDE1Address(const QString& address, const QString& name);
    Q_INVOKABLE void clearSavedDE1();

    Q_INVOKABLE void openLocationSettings();
    Q_INVOKABLE void openBluetoothSettings();

    // Reset connection state flags so retry attempts can proceed
    void resetScaleConnectionState();

    // Scale debug logging
    Q_INVOKABLE void clearScaleLog();
    Q_INVOKABLE void shareScaleLog();
    Q_INVOKABLE QString getScaleLogPath() const;
    void appendScaleLog(const QString& message);  // For use by scale implementations

public slots:
    Q_INVOKABLE void tryDirectConnectToDE1();
    Q_INVOKABLE void tryDirectConnectToScale();
    Q_INVOKABLE void scanForDevices();  // User-initiated scan for DE1, scales, and refractometers
    Q_INVOKABLE void startScan();  // Start scanning for DE1 and scales
    void stopScan();
    void clearDevices();

signals:
    void scanningChanged();
    void bluetoothAvailableChanged();
    void devicesChanged();
    void scalesChanged();
    void scaleConnectionFailedChanged();
    void de1Discovered(const QBluetoothDeviceInfo& device);
    void scaleDiscovered(const QBluetoothDeviceInfo& device, const QString& type);
    void errorOccurred(const QString& error);
    void de1LogMessage(const QString& message);
    void scaleLogMessage(const QString& message);
    void flowScaleFallback();  // Emitted when no physical scale found, using FlowScale
    void scaleDisconnected();  // Emitted when physical scale disconnects
    void scanStarted();  // Emitted when BLE scan actually begins
    void disabledChanged();
    void disconnectScaleRequested();  // Emitted when switching to a different scale, BLE is disabled, or saved scale is cleared
    void refractometersChanged();
    void refractometerConnectedChanged();
    void refractometerDiscovered(const QBluetoothDeviceInfo& device);
    void disconnectRefractometerRequested();


private slots:
    void onDeviceDiscovered(const QBluetoothDeviceInfo& device);
    void onScanFinished();
    void onScanError(QBluetoothDeviceDiscoveryAgent::Error error);
    void onScaleConnectedChanged();
    void onScaleConnectionTimeout();
    void onHostModeStateChanged(QBluetoothLocalDevice::HostMode mode);

private:
    bool isDE1Device(const QBluetoothDeviceInfo& device) const;
    QString getScaleType(const QBluetoothDeviceInfo& device) const;
    void requestBluetoothPermission();
    void doStartScan();
    void ensureDiscoveryAgent();

#ifndef Q_OS_IOS
    QBluetoothLocalDevice* m_localDevice = nullptr;
#endif
    QBluetoothDeviceDiscoveryAgent* m_discoveryAgent = nullptr;
    QList<QBluetoothDeviceInfo> m_de1Devices;
    QList<QPair<QBluetoothDeviceInfo, QString>> m_scales;  // device, type
    bool m_scanning = false;
    bool m_permissionRequested = false;
    bool m_scanningForScales = false;  // True when scanning for scales (user or auto-reconnect)
    bool m_userInitiatedScaleScan = false;  // True only for user-initiated scan (show all scales)
    bool m_scaleConnectionFailed = false;
    ScaleDevice* m_scaleDevice = nullptr;
    QTimer* m_scaleConnectionTimer = nullptr;

    // Saved scale for direct wake connection
    QString m_savedScaleAddress;
    QString m_savedScaleType;
    QString m_savedScaleName;

    // Saved DE1 for direct wake connection
    QString m_savedDE1Address;
    QString m_savedDE1Name;

    // Prevents showing "No Scale Found" dialog more than once per session
    bool m_flowScaleFallbackEmitted = false;

    // Simulator mode - disable all BLE operations
    bool m_disabled = false;

    // Direct connect state - prevents duplicate connections from scan
    bool m_directConnectInProgress = false;
    QString m_directConnectAddress;

    // Refractometer
    QList<QBluetoothDeviceInfo> m_refractometerDevices;
    QString m_savedRefractometerAddress;
    QString m_savedRefractometerName;
    DiFluidR2* m_refractometerDevice = nullptr;

    // Scale debug log
    QStringList m_scaleLogMessages;
    QString m_scaleLogFilePath;
    void writeScaleLogToFile();
};
