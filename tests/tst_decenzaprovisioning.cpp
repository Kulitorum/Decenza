// Unit tests for DecenzaProvisioningClient::parseStatus.
//
// The BLE state machine is impractical to unit-test without a fake
// QLowEnergyController (Qt doesn't ship one), so this test focuses on the
// pure protocol-parsing helper. The state machine itself is covered by
// manual smoke testing against real DecenzaScale hardware (see Phase 7 in
// the change's tasks.md).

#include "ble/scales/decenzaprovisioningclient.h"

#include <QByteArray>
#include <QTest>

class tst_DecenzaProvisioning : public QObject {
    Q_OBJECT

private slots:
    void parseStatusReturnsIdleOnTooFewBytes();
    void parseStatusDecodesConnectedIp();
    void parseStatusOmitsIpForNonConnectedStates();
    void parseStatusDecodesNegativeRssi();
    void parseStatusReportsErrorByte();
};

void tst_DecenzaProvisioning::parseStatusReturnsIdleOnTooFewBytes() {
    auto r = DecenzaProvisioningClient::parseStatus(QByteArray::fromHex("020000"));
    QCOMPARE(static_cast<int>(r.state),
             static_cast<int>(DecenzaProvisioningClient::State::Idle));
    QVERIFY(r.ip.isEmpty());
    QCOMPARE(static_cast<int>(r.err), 0);
}

void tst_DecenzaProvisioning::parseStatusDecodesConnectedIp() {
    // [state=2 Connected, rssi=-50 (0xCE), ip=192.168.1.42, err=0]
    QByteArray bytes;
    bytes.append(static_cast<char>(0x02));
    bytes.append(static_cast<char>(0xCE));
    bytes.append(static_cast<char>(192));
    bytes.append(static_cast<char>(168));
    bytes.append(static_cast<char>(1));
    bytes.append(static_cast<char>(42));
    bytes.append(static_cast<char>(0x00));
    auto r = DecenzaProvisioningClient::parseStatus(bytes);
    QCOMPARE(static_cast<int>(r.state),
             static_cast<int>(DecenzaProvisioningClient::State::Connected));
    QCOMPARE(r.ip, QStringLiteral("192.168.1.42"));
    QCOMPARE(static_cast<int>(r.rssi), -50);
    QCOMPARE(static_cast<int>(r.err), 0);
}

void tst_DecenzaProvisioning::parseStatusOmitsIpForNonConnectedStates() {
    QByteArray bytes;
    bytes.append(static_cast<char>(0x01)); // Connecting
    bytes.append(static_cast<char>(0));
    bytes.append(static_cast<char>(10));
    bytes.append(static_cast<char>(0));
    bytes.append(static_cast<char>(0));
    bytes.append(static_cast<char>(1));
    bytes.append(static_cast<char>(0));
    auto r = DecenzaProvisioningClient::parseStatus(bytes);
    QCOMPARE(static_cast<int>(r.state),
             static_cast<int>(DecenzaProvisioningClient::State::Connecting));
    QVERIFY(r.ip.isEmpty());
}

void tst_DecenzaProvisioning::parseStatusDecodesNegativeRssi() {
    QByteArray bytes;
    bytes.append(static_cast<char>(0x02));
    bytes.append(static_cast<char>(0x80)); // -128 dBm — extreme but within int8 range
    bytes.append(static_cast<char>(127));
    bytes.append(static_cast<char>(0));
    bytes.append(static_cast<char>(0));
    bytes.append(static_cast<char>(1));
    bytes.append(static_cast<char>(0));
    auto r = DecenzaProvisioningClient::parseStatus(bytes);
    QCOMPARE(static_cast<int>(r.rssi), -128);
}

void tst_DecenzaProvisioning::parseStatusReportsErrorByte() {
    QByteArray bytes;
    bytes.append(static_cast<char>(0x03)); // Failed
    bytes.append(static_cast<char>(0));
    bytes.append(static_cast<char>(0));
    bytes.append(static_cast<char>(0));
    bytes.append(static_cast<char>(0));
    bytes.append(static_cast<char>(0));
    bytes.append(static_cast<char>(0x07)); // Arbitrary error code
    auto r = DecenzaProvisioningClient::parseStatus(bytes);
    QCOMPARE(static_cast<int>(r.state),
             static_cast<int>(DecenzaProvisioningClient::State::Failed));
    QCOMPARE(static_cast<int>(r.err), 7);
}

QTEST_MAIN(tst_DecenzaProvisioning)
#include "tst_decenzaprovisioning.moc"
