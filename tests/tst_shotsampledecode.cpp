#include <QtTest>

#include "ble/de1device.h"

// Guards the two temperature setpoints in the DE1 ShotSample packet.
//
// The packet carries SetMixTemp (water-in target) and SetHeadTemp (basket
// target) at adjacent offsets, and the two BLE specs disagree on where:
//
//              SetMixTemp   SetHeadTemp
//   19-byte:   bytes 11-12  bytes 13-14
//   17-byte:   bytes 8-9    bytes 10-11
//
// Transposing them is silent — both decode to plausible brew temperatures, and
// the only visible symptom is two swapped lines on a graph. These tests pin
// each field to a distinct value so a swap fails loudly. setTempGoal must stay
// SetHeadTemp: it feeds Visualizer's `temperature.goal`, which Visualizer
// labels "Basket Temperature Goal".
class tst_ShotSampleDecode : public QObject {
    Q_OBJECT

private:
    static void putShort(QByteArray& d, int offset, double celsius) {
        quint16 raw = static_cast<quint16>(qRound(celsius * 256.0));
        d[offset] = static_cast<char>((raw >> 8) & 0xFF);
        d[offset + 1] = static_cast<char>(raw & 0xFF);
    }

    // Captures the sample the device emits for a given raw packet.
    static ShotSample decode(const QByteArray& packet) {
        DE1Device device;
        ShotSample captured;
        bool got = false;
        QObject::connect(&device, &DE1Device::shotSampleReceived,
                         [&](const ShotSample& s) { captured = s; got = true; });
        device.parseShotSample(packet);
        [&] { QVERIFY(got); }();
        return captured;
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void newSpecDecodesBothSetpoints() {
        QByteArray d(19, '\0');
        putShort(d, 6, 91.0);    // MixTemp (measured)
        putShort(d, 11, 94.5);   // SetMixTemp
        putShort(d, 13, 92.0);   // SetHeadTemp

        ShotSample s = decode(d);

        QVERIFY(qAbs(s.setMixTempGoal - 94.5) < 0.01);
        QVERIFY(qAbs(s.setTempGoal - 92.0) < 0.01);
        // Distinct values in, distinct values out — a transposition would still
        // satisfy each check above individually if they happened to match.
        QVERIFY(qAbs(s.setMixTempGoal - s.setTempGoal) > 1.0);
    }

    void oldSpecDecodesBothSetpoints() {
        QByteArray d(17, '\0');
        putShort(d, 4, 91.0);    // MixTemp (measured)
        putShort(d, 8, 94.5);    // SetMixTemp
        putShort(d, 10, 92.0);   // SetHeadTemp

        ShotSample s = decode(d);

        QVERIFY(qAbs(s.setMixTempGoal - 94.5) < 0.01);
        QVERIFY(qAbs(s.setTempGoal - 92.0) < 0.01);
        QVERIFY(qAbs(s.setMixTempGoal - s.setTempGoal) > 1.0);
    }

    // The measured mix temperature sits two bytes before SetMixTemp in the new
    // spec. Reading the goal off the wrong short would silently return it.
    void mixGoalIsNotTheMeasuredMixTemp() {
        QByteArray d(19, '\0');
        putShort(d, 6, 91.0);    // MixTemp (measured)
        putShort(d, 11, 94.5);   // SetMixTemp
        putShort(d, 13, 92.0);   // SetHeadTemp

        ShotSample s = decode(d);

        QVERIFY(qAbs(s.mixTemp - 91.0) < 0.01);
        QVERIFY(qAbs(s.setMixTempGoal - 91.0) > 1.0);
    }
};

QTEST_MAIN(tst_ShotSampleDecode)
#include "tst_shotsampledecode.moc"
