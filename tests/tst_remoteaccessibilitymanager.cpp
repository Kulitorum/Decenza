#include <QtTest>

#include <QMetaMethod>
#include <QQmlEngine>
#include <QSignalSpy>

#include "webclient/remoteaccessibilitymanager.h"
#include "webclient/remotesessionbackend.h"

class tst_RemoteAccessibilityManager final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void engineFactoryAndNoSessionKeepTapAnnouncementsDisabled()
    {
        QQmlEngine engine;
        RemoteAccessibilityManager* manager = RemoteAccessibilityManager::create(&engine, nullptr);
        QVERIFY(manager);
        QCOMPARE(manager->parent(), &engine);
        QVERIFY(!manager->enabled());

        RemoteSessionBackend session;
        manager->setSession(&session);
        QCOMPARE(manager->session(), &session);
        QVERIFY(!manager->enabled());
        manager->announce(QStringLiteral("This is ignored until the native preference is live"));

        manager->setSession(nullptr);
        QCOMPARE(manager->session(), nullptr);
        QVERIFY(!manager->enabled());
    }

    void announceLabelIsInvokableAndUsesTheSameSessionGate()
    {
        QQmlEngine engine;
        RemoteAccessibilityManager* manager = RemoteAccessibilityManager::create(&engine, nullptr);
        QVERIFY(manager);
        QVERIFY(!manager->enabled());

        const int methodIndex = manager->metaObject()->indexOfMethod("announceLabel(QString)");
        QVERIFY(methodIndex >= 0);
        const QMetaMethod method = manager->metaObject()->method(methodIndex);
        QVERIFY(method.invoke(manager, Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("Readout value"))));
        QVERIFY(!manager->enabled());
    }

    void cleanForSpeechUsesTheSharedNativeLabelFormatting()
    {
        QQmlEngine engine;
        RemoteAccessibilityManager* manager = RemoteAccessibilityManager::create(&engine, nullptr);
        QVERIFY(manager);
        QCOMPARE(manager->cleanForSpeech(QStringLiteral("gagne_88C.json")),
                 QStringLiteral("gagne 88 degrees Celsius"));
        QCOMPARE(manager->cleanForSpeech(QStringLiteral("36ml_18g-9bar.tcl")),
                 QStringLiteral("36 milliliters 18 grams 9 bar"));
        QCOMPARE(manager->cleanForSpeech(QString()), QString());
    }

    void lastAnnouncedItemTracksLifetime()
    {
        QQmlEngine engine;
        RemoteAccessibilityManager* manager = RemoteAccessibilityManager::create(&engine, nullptr);
        QSignalSpy changed(manager, &RemoteAccessibilityManager::lastAnnouncedItemChanged);
        auto* item = new QObject;
        manager->setLastAnnouncedItem(item);
        QCOMPARE(manager->lastAnnouncedItem(), item);
        QCOMPARE(changed.count(), 1);

        delete item;
        QVERIFY(manager->lastAnnouncedItem() == nullptr);
        QCOMPARE(changed.count(), 2);
        manager->setLastAnnouncedItem(nullptr);
        QCOMPARE(changed.count(), 2);
    }
};

QTEST_GUILESS_MAIN(tst_RemoteAccessibilityManager)

#include "tst_remoteaccessibilitymanager.moc"
