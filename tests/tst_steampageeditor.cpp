#include <QtTest>
#include <QSignalSpy>
#include <QRegularExpression>

#include "ble/de1device.h"
#include "ble/scaledevice.h"
#include "controllers/steampageeditor.h"
#include "core/appsettings.h"
#include "core/settings.h"
#include "core/uiworkstats.h"
#include "machine/machinestate.h"
#include "mocks/MockScaleDevice.h"

class tst_SteamPageEditor : public QObject
{
    Q_OBJECT

    struct Fixture {
        DE1Device device;
        Settings settings;
        MockScaleDevice scale;
        MachineState machine{&device};
        SteamPageEditor editor{settings.brew(), &machine};

        Fixture()
        {
            device.setSimulationMode(true);
            device.setSimulatedState(DE1::State::Idle, DE1::SubState::Ready);
            machine.setSettings(&settings);
            machine.setScale(&scale);
            scale.mockSetConnected(true);
            scale.mockSetWeight(0.0);
            auto* brew = settings.brew();
            while (brew->steamPitcherCount() > 0)
                brew->removeSteamPitcherPreset(0);
            brew->setSelectedSteamCup(0);
            brew->addSteamPitcherPreset(QStringLiteral("Test pitcher"), 30, 150, 160.0);
            brew->setSelectedSteamCup(0);
        }

        QString revision() const { return editor.editorRevision(); }
        QString requestId() const { return editor.newRequestId(); }
    };

private slots:
    void init()
    {
        QTest::failOnWarning();
        AppSettings store;
        store.clear();
        store.sync();
    }

    void unchangedEditorReadoutsComputeRevisionOnce()
    {
        Fixture f;
        auto& counter = UiWorkStats::counter(UiWorkStats::Kind::SteamEditorRevision);
        const auto before = counter.lifetimeCalls;
        const QString revision = f.revision();
        for (int i = 0; i < 100; ++i) QCOMPARE(f.revision(), revision);
        QCOMPARE(counter.lifetimeCalls - before, quint64(1));
    }

    void revisionInvalidatesBeforeNotifying_data()
    {
        QTest::addColumn<int>("input");
        const QStringList names{"pitcher-presets", "selected-pitcher", "temperature", "timeout",
                                "flow", "milk-auto-capture", "seconds-per-gram"};
        for (qsizetype i = 0; i < names.size(); ++i)
            QTest::newRow(qPrintable(names[i])) << int(i);
    }

    void revisionInvalidatesBeforeNotifying()
    {
        QFETCH(int, input);
        Fixture f;
        auto* brew = f.settings.brew();
        brew->addSteamPitcherPreset(QStringLiteral("Second pitcher"), 40, 160, 155);
        const QString previous = f.revision();
        QString observed;
        connect(&f.editor, &SteamPageEditor::editorRevisionChanged, &f.editor,
                [&] { observed = f.revision(); });
        switch (input) {
        case 0: brew->setSteamPitcherWeight(0, 200); break;
        case 1: brew->setSelectedSteamCup(1); break;
        case 2: brew->setSteamTemperature(brew->steamTemperature() + 1); break;
        case 3: brew->setSteamTimeout(brew->steamTimeout() + 1); break;
        case 4: brew->setSteamFlow(brew->steamFlow() + 1); break;
        case 5: brew->setMilkAutoCaptureEnabled(!brew->milkAutoCaptureEnabled()); break;
        case 6: brew->setSteamSecondsPerGram(brew->steamSecondsPerGram() + 0.01); break;
        }
        const QString current = f.revision();
        QVERIFY(current != previous);
        QCOMPARE(observed, current);
        auto& counter = UiWorkStats::counter(UiWorkStats::Kind::SteamEditorRevision);
        const auto afterChange = counter.lifetimeCalls;
        for (int i = 0; i < 100; ++i) QCOMPARE(f.revision(), current);
        QCOMPARE(counter.lifetimeCalls, afterChange);
    }

    void renameChangesOnlyNameAndChecksRevision()
    {
        Fixture f;
        auto* brew = f.settings.brew();
        brew->setSteamPitcherWeight(0, 135.5);
        const auto before = brew->getSteamPitcherPreset(0);
        QSignalSpy completed(&f.editor, &SteamPageEditor::operationCompleted);

        QVERIFY(f.editor.renamePreset(f.requestId(), f.revision(), 0,
                                      QStringLiteral("Test pitcher"),
                                      QStringLiteral("Renamed pitcher")));
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.at(0).at(1).toString(), QStringLiteral("rename-preset"));
        QVERIFY(completed.at(0).at(2).toBool());
        const auto after = brew->getSteamPitcherPreset(0);
        QCOMPARE(after.value(QStringLiteral("name")).toString(), QStringLiteral("Renamed pitcher"));
        QCOMPARE(after.value(QStringLiteral("duration")), before.value(QStringLiteral("duration")));
        QCOMPARE(after.value(QStringLiteral("flow")), before.value(QStringLiteral("flow")));
        QCOMPARE(after.value(QStringLiteral("temperature")), before.value(QStringLiteral("temperature")));
        QCOMPARE(after.value(QStringLiteral("pitcherWeightG")), before.value(QStringLiteral("pitcherWeightG")));

        const QString staleRevision = f.revision();
        brew->setSteamTimeout(brew->steamTimeout() + 1);
        QVERIFY(!f.editor.renamePreset(f.requestId(), staleRevision, 0,
                                       QStringLiteral("Renamed pitcher"),
                                       QStringLiteral("Stale rename")));
        QCOMPARE(brew->getSteamPitcherPreset(0).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Renamed pitcher"));
        QCOMPARE(completed.size(), 2);
        QVERIFY(!completed.at(1).at(2).toBool());
    }

    void tuningOperationsCommitOnceAndPreserveOtherFields()
    {
        Fixture f;
        auto* brew = f.settings.brew();
        QSignalSpy flowCommit(&f.editor, &SteamPageEditor::steamFlowCommitRequested);
        QSignalSpy temperatureCommit(&f.editor, &SteamPageEditor::steamTemperatureCommitRequested);

        QVERIFY(f.editor.setPresetDuration(f.requestId(), f.revision(), 0,
                                           QStringLiteral("Test pitcher"), 42));
        QCOMPARE(brew->steamTimeout(), 42);
        QCOMPARE(brew->getSteamPitcherPreset(0).value(QStringLiteral("duration")).toInt(), 42);

        QVERIFY(f.editor.setPresetFlow(f.requestId(), f.revision(), 0,
                                       QStringLiteral("Test pitcher"), 175));
        QCOMPARE(flowCommit.size(), 1);
        QCOMPARE(flowCommit.first().first().toInt(), 175);
        QCOMPARE(brew->getSteamPitcherPreset(0).value(QStringLiteral("flow")).toInt(), 175);
        QCOMPARE(brew->steamFlow(), 175);

        QVERIFY(f.editor.setPresetTemperatureC(f.requestId(), f.revision(), 0,
                                               QStringLiteral("Test pitcher"), 154.0));
        QCOMPARE(temperatureCommit.size(), 1);
        QCOMPARE(temperatureCommit.first().first().toDouble(), 154.0);
        QCOMPARE(brew->getSteamPitcherPreset(0).value(QStringLiteral("temperature")).toDouble(), 154.0);
    }

    void storeCurrentWeightIsCapturedByHostAndReturned()
    {
        Fixture f;
        f.scale.mockSetWeight(182.4);
        QSignalSpy completed(&f.editor, &SteamPageEditor::operationCompleted);

        QVERIFY(f.editor.storeCurrentPitcherWeight(f.requestId(), f.revision(), 0,
                                                   QStringLiteral("Test pitcher")));
        QCOMPARE(completed.size(), 1);
        QVERIFY(completed.first().at(2).toBool());
        const QVariantMap result = completed.first().at(4).toMap();
        QCOMPARE(result.value(QStringLiteral("weightG")).toDouble(), 182.4);
        QCOMPARE(f.settings.brew()->getSteamPitcherPreset(0)
                     .value(QStringLiteral("pitcherWeightG")).toDouble(), 182.4);
    }

    void createAndClearReturnMatchingOperationResults()
    {
        Fixture f;
        QSignalSpy completed(&f.editor, &SteamPageEditor::operationCompleted);

        QVERIFY(f.editor.createPreset(f.requestId(), f.revision(), QStringLiteral("Second pitcher")));
        QCOMPARE(completed.size(), 1);
        QVERIFY(completed.at(0).at(2).toBool());
        QCOMPARE(completed.at(0).at(4).toMap().value(QStringLiteral("index")).toInt(), 1);

        f.settings.brew()->setSteamPitcherWeight(0, 123.0);
        QVERIFY(f.editor.clearPitcherWeight(f.requestId(), f.revision(),
                                            0, QStringLiteral("Test pitcher")));
        QCOMPARE(completed.size(), 2);
        QVERIFY(completed.at(1).at(2).toBool());
        QCOMPARE(completed.at(1).at(4).toMap().value(QStringLiteral("weightG")).toDouble(), 0.0);
    }

    void completionReentrancyCannotClearBusyBeforeOuterResult()
    {
        Fixture f;
        QSignalSpy completed(&f.editor, &SteamPageEditor::operationCompleted);
        bool attemptedReentrantEdit = false;
        QList<bool> busyDuringCompletion;
        connect(&f.editor, &SteamPageEditor::operationCompleted, &f.editor,
                [&](const QString&, const QString& operation, bool success,
                    const QString&, const QVariantMap&, const QString&) {
            busyDuringCompletion.append(f.editor.busy());
            if (!attemptedReentrantEdit && operation == QStringLiteral("rename-preset") && success) {
                attemptedReentrantEdit = true;
                QVERIFY(!f.editor.setMilkAutoCaptureEnabled(
                    f.requestId(), f.revision(), true));
            }
        });

        QVERIFY(f.editor.renamePreset(f.requestId(), f.revision(), 0,
                                      QStringLiteral("Test pitcher"), QStringLiteral("Renamed")));
        QCOMPARE(completed.size(), 2);
        QCOMPARE(completed.at(0).at(1).toString(), QStringLiteral("rename-preset"));
        QVERIFY(completed.at(0).at(2).toBool());
        QCOMPARE(completed.at(0).at(5).toString(), f.revision());
        QCOMPARE(completed.at(1).at(1).toString(), QStringLiteral("set-milk-auto-capture"));
        QVERIFY(!completed.at(1).at(2).toBool());
        QCOMPARE(busyDuringCompletion, QList<bool>({true, true}));
        QVERIFY(!f.editor.busy());
    }

    void rejectsInvalidNamesWithoutWriting()
    {
        Fixture f;
        QSignalSpy completed(&f.editor, &SteamPageEditor::operationCompleted);
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("refusing a duplicate steam pitcher named Heater off"));
        QVERIFY(!f.editor.createPreset(f.requestId(), f.revision(), QStringLiteral("Heater off")));
        QCOMPARE(f.settings.brew()->steamPitcherCount(), 1);
        QCOMPARE(completed.size(), 1);
        QVERIFY(!completed.first().at(2).toBool());
    }
};

QTEST_MAIN(tst_SteamPageEditor)
#include "tst_steampageeditor.moc"
