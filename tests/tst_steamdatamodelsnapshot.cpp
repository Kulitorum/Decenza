#include <QtTest>

#include <QSignalSpy>
#include <limits>

#include "models/steamdatamodel.h"
#include "network/remotesteamgraphsnapshot.h"

class tst_SteamDataModelSnapshot final : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(); }

    void notificationsCanDeleteModel_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QString>("signalName");
        for (const char* operation : {"clear", "replace", "append", "flush"}) {
            for (const char* signalName : {"revision", "cleared", "maxTime", "rawTime", "flowGoals"}) {
                const QString action = QString::fromLatin1(operation);
                const QString notification = QString::fromLatin1(signalName);
                if (action == QLatin1String("flush")
                    && (notification == QLatin1String("revision") || notification == QLatin1String("cleared")))
                    continue;
                if ((action == QLatin1String("replace") || action == QLatin1String("append"))
                    && notification == QLatin1String("cleared"))
                    continue;
                QTest::newRow(qPrintable(action + QLatin1Char('-') + notification))
                    << action << notification;
            }
        }
    }

    void notificationsCanDeleteModel()
    {
        QFETCH(QString, operation);
        QFETCH(QString, signalName);
        QPointer<SteamDataModel> model = new SteamDataModel;
        model->addSample(6.0, 1.0, 2.0, 130.0);
        model->addFlowGoalPoint(0.0, 0.5);
        const auto signal = signalName == QLatin1String("revision") ? &SteamDataModel::revisionChanged
            : signalName == QLatin1String("cleared") ? &SteamDataModel::cleared
            : signalName == QLatin1String("maxTime") ? &SteamDataModel::maxTimeChanged
            : signalName == QLatin1String("rawTime") ? &SteamDataModel::rawTimeChanged
            : &SteamDataModel::flowGoalPointsChanged;
        connect(model.data(), signal, this, [&] { delete model.data(); });
        if (operation == QLatin1String("clear")) {
            model->clear();
        } else if (operation == QLatin1String("replace")) {
            QVERIFY(model->replaceSnapshot(7, 6.0, {{0.0, 1.0}}, {{0.0, 2.0}},
                                           {{0.0, 130.0}}, {}));
        } else if (operation == QLatin1String("append")) {
            const QVector<QPointF> goals;
            QVERIFY(model->appendSnapshot(model->revision(), model->revision() + 1, 7,
                {{7, 1}}, {{7, 2}}, {{7, 130}}, &goals));
        } else {
            QVERIFY(QMetaObject::invokeMethod(model.data(), "onFlushTimerTick", Qt::DirectConnection));
        }
        QVERIFY(model.isNull());
    }

    void replacementIsAtomicAndNotifiesCompleteGraph()
    {
        SteamDataModel model;
        model.addSample(1.0, 1.0, 2.0, 130.0);
        QCOMPARE(model.sampleCount(), 1);
        const qint64 oldRevision = model.revision();

        QSignalSpy revisionSpy(&model, &SteamDataModel::revisionChanged);
        QSignalSpy flushedSpy(&model, &SteamDataModel::flushed);
        const QVector<QPointF> pressure{{0.0, 0.5}, {4.0, 1.5}};
        const QVector<QPointF> flow{{0.0, 0.0}, {4.0, 3.0}};
        const QVector<QPointF> temperature{{0.0, 132.0}, {4.0, 141.0}};
        const QVector<QPointF> goals{{0.0, 0.0}, {30.0, 5.0}};

        QVERIFY(model.replaceSnapshot(25, 4.0, pressure, flow, temperature, goals));
        QCOMPARE(model.revision(), 25);
        QVERIFY(model.revision() > oldRevision);
        QCOMPARE(model.rawTime(), 4.0);
        QCOMPARE(model.maxTime(), 5.0);
        QCOMPARE(model.sampleCount(), 2);
        QCOMPARE(model.pressureData(), pressure);
        QCOMPARE(model.flowData(), flow);
        QCOMPARE(model.temperatureData(), temperature);
        QCOMPARE(model.flowGoalPoints().size(), 2);
        QCOMPARE(revisionSpy.count(), 1);
        QCOMPARE(flushedSpy.count(), 1);

        RemoteSteamGraphSnapshot::Snapshot wireSnapshot;
        wireSnapshot.packageId = QStringLiteral("package-test");
        wireSnapshot.hostId = QStringLiteral("7a54d42f-93de-48a7-ae78-38925bd0c3e1");
        wireSnapshot.revision = model.revision();
        wireSnapshot.rawTime = model.rawTime();
        wireSnapshot.pressure = model.pressureData();
        wireSnapshot.flow = model.flowData();
        wireSnapshot.temperature = model.temperatureData();
        wireSnapshot.flowGoalPoints = goals;
        const auto serialized = RemoteSteamGraphSnapshot::serialize(wireSnapshot);
        QVERIFY(serialized.ok);
        RemoteSteamGraphSnapshot::Snapshot parsed;
        QString parseError;
        QVERIFY(RemoteSteamGraphSnapshot::parse(serialized.body, wireSnapshot.packageId,
                                                wireSnapshot.hostId, wireSnapshot.revision,
                                                &parsed, &parseError));
        QCOMPARE(parsed.pressure, model.pressureData());
        QCOMPARE(parsed.flow, model.flowData());
        QCOMPARE(parsed.temperature, model.temperatureData());
    }

    void unchangedFlowGoalsDoNotNotifyForEverySample()
    {
        SteamDataModel model;
        const QVector<QPointF> goals{{0.0, 1.0}, {30.0, 3.0}};
        QVERIFY(model.replaceSnapshot(1, 0, {{0, 1}}, {{0, 2}}, {{0, 130}}, goals));
        QSignalSpy changes(&model, &SteamDataModel::flowGoalPointsChanged);
        for (int i = 1; i <= 100; ++i)
            QVERIFY(model.replaceSnapshot(i + 1, i, {{double(i), 1}},
                {{double(i), 2}}, {{double(i), 130}}, goals));
        QCOMPARE(changes.size(), 0);
    }

    void invalidReplacementLeavesPreviousSnapshotUntouched()
    {
        SteamDataModel model;
        QVERIFY(model.replaceSnapshot(7, 2.0, {{0.0, 1.0}}, {{0.0, 2.0}},
                                      {{0.0, 130.0}}, {{0.0, 0.0}}));
        const qint64 originalRevision = model.revision();
        const auto originalPressure = model.pressureData();

        QVERIFY(!model.replaceSnapshot(8, 2.0, {{0.0, 9.0}}, {{1.0, 8.0}},
                                       {{0.0, 131.0}}, {{0.0, 0.0}}));
        QCOMPARE(model.revision(), originalRevision);
        QCOMPARE(model.pressureData(), originalPressure);

        QVERIFY(!model.replaceSnapshot(8, 2.0, {{0.0, 9.0}}, {{0.0, 8.0}},
                                       {{0.0, std::numeric_limits<double>::infinity()}},
                                       {{0.0, 0.0}}));
        QCOMPARE(model.pressureData(), originalPressure);
    }

    void invalidAppendIsAtomicAndValidAppendPreservesGoals()
    {
        SteamDataModel model;
        const QVector<QPointF> goals{{0, 2}, {30, 3}};
        QVERIFY(model.replaceSnapshot(7, 2, {{2, 1}}, {{2, 2}}, {{2, 130}}, goals));
        QSignalSpy goalChanges(&model, &SteamDataModel::flowGoalPointsChanged);
        QSignalSpy revisions(&model, &SteamDataModel::revisionChanged);
        QVERIFY(!model.appendSnapshot(6, 8, 3, {{3, 1}}, {{3, 2}}, {{3, 130}}));
        QVERIFY(!model.appendSnapshot(7, 8, 3, {{3, 1}}, {{2, 2}}, {{3, 130}}));
        QVERIFY(!model.appendSnapshot(7, 8, 3, {{1, 1}}, {{1, 2}}, {{1, 130}}));
        QVERIFY(!model.appendSnapshot(7, 8, 1, {}, {}, {}));
        QCOMPARE(model.sampleCount(), 1);
        QCOMPARE(model.revision(), 7);
        QCOMPARE(revisions.size(), 0);
        QVERIFY(model.appendSnapshot(7, 8, 3, {{3, 1}}, {{3, 2}}, {{3, 130}}));
        QCOMPARE(model.sampleCount(), 2);
        QCOMPARE(model.pressureData().first(), QPointF(2, 1));
        QCOMPARE(model.pressureData().last(), QPointF(3, 1));
        QCOMPARE(goalChanges.size(), 0);
        QCOMPARE(revisions.size(), 1);
        QVERIFY(model.appendSnapshot(8, 9, 3, {}, {}, {}, &goals));
        QCOMPARE(goalChanges.size(), 0);
    }

    void firstZeroTimeAppendNotifiesSampleCount()
    {
        SteamDataModel model;
        QVERIFY(model.replaceSnapshot(0, 0, {}, {}, {}, {}));
        QSignalSpy readouts(&model, &SteamDataModel::rawTimeChanged);
        QVERIFY(model.appendSnapshot(0, 1, 0, {{0, 1}}, {{0, 2}}, {{0, 130}}));
        QCOMPARE(model.sampleCount(), 1);
        QCOMPARE(readouts.size(), 1);
    }

    void nativeMutationsAdvanceRevisionAndClearRemovesGraph()
    {
        SteamDataModel model;
        const qint64 initial = model.revision();
        model.addSample(1.0, 1.0, 2.0, 130.0);
        const qint64 sampleRevision = model.revision();
        QVERIFY(sampleRevision > initial);
        model.addFlowGoalPoint(0.0, 0.5);
        QVERIFY(model.revision() > sampleRevision);
        model.clear();
        QVERIFY(model.revision() > sampleRevision);
        QCOMPARE(model.sampleCount(), 0);
        QCOMPARE(model.rawTime(), 0.0);
        QVERIFY(model.flowGoalPoints().isEmpty());
    }
};

QTEST_GUILESS_MAIN(tst_SteamDataModelSnapshot)
#include "tst_steamdatamodelsnapshot.moc"
