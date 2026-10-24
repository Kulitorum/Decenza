#include <QtTest>

#include <QQmlComponent>
#include <QQmlEngine>
#include <QFile>

class GrindRowSourceQmlTest final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void productionComponentInstantiatesWithHistoryHost()
    {
        QFile file(QStringLiteral(GRIND_ROW_SOURCE_QML_FILE));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QByteArray source = file.readAll();
        const QByteArray appImport = QByteArrayLiteral("import Decenza\n");
        QCOMPARE(source.count(appImport), 1);
        source.replace(appImport, QByteArrayLiteral("import DecenzaGrindRowSourceTests 1.0\n"));

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(source, QUrl::fromLocalFile(file.fileName()));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QObject* instance = component.create();
        QVERIFY2(instance, qPrintable(component.errorString()));
        QCOMPARE(instance->property("grindWindowSteps").toInt(), 200);
        QCOMPARE(instance->property("rpmWindowSteps").toInt(), 40);
        QCOMPARE(instance->property("rpmCapable").toBool(), false);
        delete instance;
    }
};

QTEST_GUILESS_MAIN(GrindRowSourceQmlTest)

#include "tst_grindrowsourceqml.moc"
