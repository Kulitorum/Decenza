#include <QtTest>
#include <QQmlEngine>
#include <QRemoteObjectNode>

#include "remote/systemlogservice_qml.h"

class tst_SystemLogService : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void replicaExistsOnlyWhileAViewIsActive()
    {
        QRemoteObjectNode node;
        SystemLogService::setPublishedNode(&node);
        QQmlEngine engine;
        auto* service = SystemLogServiceForeign::create(&engine, nullptr);
        QVERIFY(service);
        QVERIFY(!service->state());

        auto* view = new QObject;
        service->setViewActive(view, true);
        QVERIFY(service->state());
        service->setViewActive(view, false);
        QVERIFY(!service->state());

        service->setViewActive(view, true);
        QVERIFY(service->state());
        delete view;
        QVERIFY(!service->state());

        delete service;
        SystemLogService::setPublishedNode(nullptr);
    }
};

QTEST_MAIN(tst_SystemLogService)
#include "tst_systemlogservice.moc"
