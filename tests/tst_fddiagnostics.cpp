#include <QtTest>
#include <QJsonArray>

#include "core/fddiagnostics.h"

class tst_FdDiagnostics : public QObject
{
    Q_OBJECT

private slots:
    void snapshotHasAnExplicitPlatformResult()
    {
        const QJsonObject snapshot = FdDiagnostics::snapshot();
        QVERIFY(snapshot.contains("supported"));
        if (!snapshot.value("supported").toBool()) {
            QVERIFY(!snapshot.value("error").toString().isEmpty());
            return;
        }

        QVERIFY(snapshot.value("openFdCount").toInt() > 0);
        QVERIFY(snapshot.value("descriptorKinds").isObject());
        QVERIFY(snapshot.value("descriptors").isArray());
        QVERIFY(!snapshot.value("descriptors").toArray().isEmpty());
    }
};

QTEST_MAIN(tst_FdDiagnostics)
#include "tst_fddiagnostics.moc"
