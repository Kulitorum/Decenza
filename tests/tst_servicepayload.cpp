#include <QtTest>

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>

#include "remote/servicepayload.h"
#include "remote/servicevalue.h"

class ServicePayloadTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
    void exactKeysKeepRequestSchemasClosed_data()
    {
        QTest::addColumn<QVariantMap>("fields");
        QTest::addColumn<QStringList>("keys");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("empty") << QVariantMap{} << QStringList{} << true;
        QTest::newRow("unordered")
            << QVariantMap{{QStringLiteral("index"), 2}, {QStringLiteral("value"), true}}
            << QStringList{QStringLiteral("value"), QStringLiteral("index")} << true;
        QTest::newRow("missing") << QVariantMap{{QStringLiteral("value"), true}}
            << QStringList{QStringLiteral("value"), QStringLiteral("index")} << false;
        QTest::newRow("extra")
            << QVariantMap{{QStringLiteral("value"), true}, {QStringLiteral("extra"), 1}}
            << QStringList{QStringLiteral("value")} << false;
        QTest::newRow("different-key-same-count") << QVariantMap{{QStringLiteral("other"), true}}
            << QStringList{QStringLiteral("value")} << false;
        QTest::newRow("case-sensitive") << QVariantMap{{QStringLiteral("Value"), true}}
            << QStringList{QStringLiteral("value")} << false;
        QTest::newRow("duplicate-schema-key")
            << QVariantMap{{QStringLiteral("value"), true}, {QStringLiteral("extra"), 1}}
            << QStringList{QStringLiteral("value"), QStringLiteral("value")} << false;
    }

    void exactKeysKeepRequestSchemasClosed()
    {
        QFETCH(QVariantMap, fields);
        QFETCH(QStringList, keys);
        QFETCH(bool, accepted);
        QCOMPARE(ServiceValue::hasExactKeys(fields, keys), accepted);
        // Keep the existing QLatin1StringView call sites on the same predicate.
        QVERIFY(ServiceValue::hasExactKeys({{QStringLiteral("name"), QString()}},
                                          {QLatin1StringView("name")}));
    }

    void countsExactBoundaryWithoutCopying()
    {
        const QVariantMap payload{{QStringLiteral("title"), QStringLiteral("Crème ☕")},
                                  {QStringLiteral("points"), QVariantList{1.25, 3.5, 8.0}}};
        QByteArray serialized;
        QDataStream stream(&serialized, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << payload;
        QCOMPARE(stream.status(), QDataStream::Ok);

        const qint64 expected = serialized.size();
        QCOMPARE(ServicePayload::serializedSize(payload, expected, QDataStream::Qt_6_0), expected);
        QVERIFY(ServicePayload::fits(payload, expected, QDataStream::Qt_6_0));
        QCOMPARE(ServicePayload::serializedSize(payload, expected - 1, QDataStream::Qt_6_0), -1);
        QVERIFY(!ServicePayload::fits(payload, expected - 1, QDataStream::Qt_6_0));
    }

    void defaultVersionMatchesQtStream()
    {
        const QVariantMap payload{{QStringLiteral("label"), QStringLiteral("雪山")}};
        QByteArray serialized;
        QDataStream stream(&serialized, QIODevice::WriteOnly);
        stream << payload;
        QCOMPARE(stream.status(), QDataStream::Ok);
        QCOMPARE(ServicePayload::serializedSize(payload), qint64(serialized.size()));
    }
};

QTEST_GUILESS_MAIN(ServicePayloadTest)
#include "tst_servicepayload.moc"
