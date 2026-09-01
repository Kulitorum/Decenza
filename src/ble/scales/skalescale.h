#pragma once

#include "../scaledevice.h"
#include "core/logcollapse.h"
#include "../transport/scalebletransport.h"
#include <QTimer>

class SkaleScale : public ScaleDevice {
    Q_OBJECT

public:
    explicit SkaleScale(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~SkaleScale() override;

    void connectToDevice(const QBluetoothDeviceInfo& device) override;
    QString name() const override { return m_name; }
    QString type() const override { return ScaleTypeIds::scaleTypeId(ScaleType::Skale); }

public slots:
    void tare() override;
    bool supportsTimer() const override { return true; }
    void startTimer() override;
    void stopTimer() override;
    void resetTimer() override;
    void sendKeepAlive() override;
    void sleep() override;
    void wake() override { enableLcd(); }

    // Skale-specific functions
    void enableLcd();
    void disableLcd() override;
    void enableGrams();

private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onTransportError(const QString& message);
    void onServiceDiscovered(const QBluetoothUuid& uuid);
    void onServicesDiscoveryFinished();
    void onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid);
    void onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value);

private:
#ifdef DECENZA_TESTING
    friend class tst_ScaleProtocol;
#endif
    void sendCommand(uint8_t cmd);
    // See scaleFrameShapeLine() for the once-per-shape, capped policy.
    void logFrameShapeOnce(const QString& shape, const QByteArray& data);

    ScaleBleTransport* m_transport = nullptr;
    
    // Weight frames the decoder could not read. EPISODIC — a connection ends — so
    // onTransportDisconnected() ends the run with flushAll().
    LogCollapse m_frameShapeLog{LogCollapse::kChangesOnly};
    QString m_name = "Skale";
    bool m_serviceFound = false;
    bool m_characteristicsReady = false;
};
