#include <QtTest>
#include "../src/remote/datamigrationcommandpolicy.h"

class DataMigrationCommandPolicyTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void acceptsOnlyBoundedHttpAddressesAndSixDigitCodes()
    {
        QVERIFY(DataMigrationCommandPolicy::validate(QStringLiteral("connectToServer"),
            {{QStringLiteral("serverUrl"), QStringLiteral("192.168.1.2:8888")}}));
        QVERIFY(DataMigrationCommandPolicy::validate(QStringLiteral("authenticate"),
            {{QStringLiteral("totpCode"), QStringLiteral("123456")}}));
        QVERIFY(!DataMigrationCommandPolicy::validate(QStringLiteral("authenticate"),
            {{QStringLiteral("totpCode"), QStringLiteral("12345x")}}));
        QVERIFY(!DataMigrationCommandPolicy::validate(QStringLiteral("connectToServer"),
            {{QStringLiteral("serverUrl"), QStringLiteral("http://user:pass@example.test")}}));
        QVERIFY(!DataMigrationCommandPolicy::validate(QStringLiteral("importAll"),
            {{QStringLiteral("totpCode"), QStringLiteral("123456")}}));
        QVERIFY(!DataMigrationCommandPolicy::validate(QStringLiteral("forgedOperation"), {}));
    }

    void snapshotProjectionDropsUnapprovedManifestAndDeviceFields()
    {
        const QVariantMap manifest = DataMigrationCommandPolicy::safeManifest({
            {QStringLiteral("deviceName"), QStringLiteral("Kitchen")},
            {QStringLiteral("profileCount"), 7},
            {QStringLiteral("sessionToken"), QStringLiteral("secret")},
            {QStringLiteral("settingsSize"), 1234}});
        QCOMPARE(manifest.value(QStringLiteral("deviceName")).toString(), QStringLiteral("Kitchen"));
        QVERIFY(!manifest.contains(QStringLiteral("sessionToken")));
        QVERIFY(!manifest.contains(QStringLiteral("settingsSize")));

        const QVariantList devices = DataMigrationCommandPolicy::safeDevices({QVariantMap{
            {QStringLiteral("deviceName"), QStringLiteral("Kitchen")},
            {QStringLiteral("serverUrl"), QStringLiteral("http://192.168.1.2:8888")},
            {QStringLiteral("sessionToken"), QStringLiteral("secret")}}});
        QCOMPARE(devices.size(), 1);
        QVERIFY(!devices.first().toMap().contains(QStringLiteral("sessionToken")));
    }
};

QTEST_GUILESS_MAIN(DataMigrationCommandPolicyTest)
#include "tst_datamigrationcommandpolicy.moc"
