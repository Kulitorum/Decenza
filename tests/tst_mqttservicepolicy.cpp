#include <QtTest>

#include "../src/remote/mqttservicepolicy.h"

class MqttServicePolicyTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QTest::failOnWarning(); }

    void rejectsUnknownPreferences()
    {
        QVERIFY(!MqttServicePolicy::supportedPreference(QStringLiteral("mqttPasswordConfigured")));
        QVERIFY(!MqttServicePolicy::validPreferenceValue(QStringLiteral("arbitrary"), true));
    }

    void validatesTypesAndRanges()
    {
        QVERIFY(MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttEnabled"), true));
        QVERIFY(!MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttEnabled"), 1));
        QVERIFY(MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttBrokerPort"), 65535));
        QVERIFY(!MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttBrokerPort"), 65536));
        QVERIFY(MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttPublishInterval"), 5000));
        QVERIFY(!MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttPublishInterval"), 750));
        QVERIFY(!MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttPassword"), 15));
    }

    void enforcesStringLimitsAndClientId()
    {
        QVERIFY(MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttBrokerHost"), QString()));
        QVERIFY(!MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttClientId"), QStringLiteral("  ")));
        QVERIFY(!MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttUsername"), QString(257, QLatin1Char('x'))));
        QVERIFY(MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttCaCertificate"), QString(65536, QLatin1Char('x'))));
        QVERIFY(!MqttServicePolicy::validPreferenceValue(QStringLiteral("mqttCaCertificate"), QString(65537, QLatin1Char('x'))));
    }
};

QTEST_GUILESS_MAIN(MqttServicePolicyTest)
#include "tst_mqttservicepolicy.moc"
