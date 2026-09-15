#pragma once

#include "transport/scalebletransport.h"
#include "protocol/belkaportalprotocol.h"
#include <QElapsedTimer>
#include "core/logcollapse.h"
#include <QVariantList>

// PORTAL measurements and display commands; machine/scale control stays with their drivers.
class BelkaPortalDevice : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString name READ name NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool machineBusy READ machineBusy NOTIFY machineBusyChanged)
    Q_PROPERTY(bool hasReading READ hasReading NOTIFY readingChanged)
    Q_PROPERTY(double ecRaw READ ecRaw NOTIFY readingChanged)
    Q_PROPERTY(double temperatureC READ temperatureC NOTIFY readingChanged)
    Q_PROPERTY(QString lastPacket READ lastPacket NOTIFY readingChanged)
    Q_PROPERTY(int packetCount READ packetCount NOTIFY readingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString savedAddress READ savedAddress NOTIFY savedDeviceChanged)
    Q_PROPERTY(QString savedName READ savedName NOTIFY savedDeviceChanged)
    Q_PROPERTY(bool syncDisplay READ syncDisplay WRITE setSyncDisplay NOTIFY syncDisplayChanged)
    Q_PROPERTY(bool canControlDisplay READ canControlDisplay NOTIFY stateChanged)
    Q_PROPERTY(QString displayCommandStatus READ displayCommandStatus NOTIFY displayCommandStatusChanged)

public:
    enum class State { Disconnected, Connecting, Discovering, Waiting, Streaming, Stale, Error, Disconnecting };
    Q_ENUM(State)
    using TransportFactory = std::function<ScaleBleTransport*()>;
    explicit BelkaPortalDevice(TransportFactory factory, QObject* parent = nullptr);
    explicit BelkaPortalDevice(ScaleBleTransport* transport, QObject* parent = nullptr);
    ~BelkaPortalDevice() override;
    QString state() const;
    QString name() const { return m_name; }
    bool active() const;
    bool machineBusy() const { return m_machineBusy; }
    bool hasReading() const { return m_state == State::Streaming; }
    double ecRaw() const { return m_ec; }
    double temperatureC() const { return m_temperature; }
    QString lastPacket() const { return m_lastPacket; }
    int packetCount() const { return m_packetCount; }
    QString errorMessage() const { return m_error; }
    QVariantList devices() const;
    QString savedAddress() const { return m_savedAddress; }
    QString savedName() const { return m_savedName; }
    bool syncDisplay() const { return m_syncDisplay; }
    bool canControlDisplay() const;
    QString displayCommandStatus() const { return m_displayCommandStatus; }
    void restoreSavedDevice(const QString& address, const QString& name);
    void setSyncDisplay(bool enabled);
    void setExtractionActive(bool active);
    void beginScan();
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void forgetDevice();
    Q_INVOKABLE void setGraphView(bool show);

    static bool isPortal(const QBluetoothDeviceInfo& info) {
        return info.serviceUuids().contains(QBluetoothUuid(BelkaPortalProtocol::ServiceUuid))
            || info.name().contains(QStringLiteral("PORTAL"), Qt::CaseInsensitive);
    }
    void observeDevice(const QBluetoothDeviceInfo& info);
    void setMachineBusy(bool busy);
    Q_INVOKABLE void clearDevices();
    Q_INVOKABLE void connectDevice(const QString& identifier);
    Q_INVOKABLE void disconnectDevice();

signals:
    void stateChanged();
    void readingChanged();
    void devicesChanged();
    void machineBusyChanged();
    void measurementReceived(double ecRaw, double temperatureC);
    void readingInterrupted();
    void savedDeviceChanged();
    void syncDisplayChanged();
    void displayCommandStatusChanged();
    void scanRequested();

private:
    void ensureTransport();
    void setState(State state);
    void finishDisconnect();
    void clearConnectionData();
    void finishDisplayWrite(bool succeeded);
    void fail(const QString& error);
    void receive(const QBluetoothUuid& characteristic, const QByteArray& packet, bool notification);
    void checkFreshness();
    void expireReading(qint64 ageMs);
    void tryReconnect();
    void writeGraphView(bool show);
    void stopConnection(bool manual);
    void logEvent(const QString& key, const QString& message, QtMsgType level = QtInfoMsg,
                  const QString& detail = {});
    void flushLogs();
    // Flush at shot end, faults, manual disconnect and selection changes.
    LogCollapse m_logCollapse{LogCollapse::kChangesOnly};
    QElapsedTimer m_logClock;
    bool m_loggedPacketShape = false;
    QString m_selectedAddress;

    TransportFactory m_transportFactory;
    ScaleBleTransport* m_transport = nullptr;
    QList<QBluetoothDeviceInfo> m_devices;
    QTimer m_healthTimer;
    QElapsedTimer m_lastMeasurement;
    State m_state = State::Disconnected;
    QString m_name;
    QString m_error;
    QString m_lastPacket;
    double m_ec = 0;
    double m_temperature = 0;
    int m_packetCount = 0;
    int m_properties = 0;
    int m_commandProperties = 0;
    QString m_savedAddress;
    QString m_savedName;
    QString m_displayCommandStatus;
    bool m_syncDisplay = true;
    bool m_extractionActive = false;
    bool m_displayStartedForShot = false;
    bool m_reconnectEnabled = true;
    bool m_reconnectAttempted = false;
    QList<QByteArray> m_displayWrites;
    bool m_serviceFound = false;
    bool m_machineBusy = false;

#ifdef DECENZA_TESTING
    friend class tst_BelkaPortal;
#endif
};
