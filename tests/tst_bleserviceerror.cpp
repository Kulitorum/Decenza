#include <QtTest>

#include "ble/bleserviceerror.h"

// Names for QLowEnergyService::ServiceError.
//
// The reason this is worth a test at all: a user reported "Service error: 5"
// from the DE1 link and nobody — including the debug log they attached — could
// say what 5 meant. The transport was formatting the raw enum into a dialog.
// These cases pin the numeric values to their names so the mapping cannot drift
// silently, and so the specific value from the report stays documented (#1586).
class tst_BleServiceError : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void namesMatchQtEnumValues_data() {
        QTest::addColumn<int>("rawValue");
        QTest::addColumn<QString>("expectedName");

        // Values are Qt's, asserted numerically on purpose: a user or a log only
        // ever shows the number, so the number is what has to keep its meaning.
        QTest::newRow("0 NoError") << 0 << "NoError";
        QTest::newRow("1 OperationError") << 1 << "OperationError";
        QTest::newRow("2 CharacteristicWriteError") << 2 << "CharacteristicWriteError";
        QTest::newRow("3 DescriptorWriteError") << 3 << "DescriptorWriteError";
        QTest::newRow("4 UnknownError") << 4 << "UnknownError";
        QTest::newRow("5 CharacteristicReadError") << 5 << "CharacteristicReadError";
        QTest::newRow("6 DescriptorReadError") << 6 << "DescriptorReadError";
    }

    void namesMatchQtEnumValues() {
        QFETCH(int, rawValue);
        QFETCH(QString, expectedName);
        QCOMPARE(bleServiceErrorName(
                     static_cast<QLowEnergyService::ServiceError>(rawValue)),
                 expectedName);
    }

    // The value from issue #1586, called out separately so the report stays
    // traceable to a name rather than living only in the data table above.
    void reportedValueFiveIsCharacteristicReadError() {
        QCOMPARE(bleServiceErrorName(QLowEnergyService::CharacteristicReadError),
                 QStringLiteral("CharacteristicReadError"));
        QCOMPARE(static_cast<int>(QLowEnergyService::CharacteristicReadError), 5);
    }

    // The number-fallback after the switch is deliberately NOT tested. Reaching
    // it requires an out-of-range enum value, and merely loading one is
    // undefined behaviour — UBSan flags it ("load of value 99, which is not a
    // valid value for type QLowEnergyService::ServiceError"). macOS runs UBSan
    // in recovering mode, so such a test passes locally and green-lights a
    // change that would fail the halting-mode nightly Linux job. The fallback
    // exists to satisfy the compiler's control-flow analysis, not as reachable
    // behaviour; every value Qt can actually deliver is covered above.
};

QTEST_MAIN(tst_BleServiceError)
#include "tst_bleserviceerror.moc"
