#include <QtTest>

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>

#include "remote/servicepayload.h"

class ServicePayloadTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }
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
