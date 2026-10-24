#include <QtTest>

#include "network/steamuistate.h"

class tst_SteamUiState final : public QObject
{
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }
    void defaultsMatchNativeSteamUi()
    {
        const SteamUiState state;
        QCOMPARE(state.viewMode, QStringLiteral("timer"));
        QVERIFY(!state.coachVisualEnabled);
        QVERIFY(!state.coachAudioEnabled);
        QVERIFY(state.coachCueText.isEmpty());
        QVERIFY(state.coachCueSeverity.isEmpty());
        QVERIFY(!state.coachCueActive);
        QVERIFY(!state.durationMilkDerived);
        QVERIFY(state.extractionAnnouncementsEnabled);
        QCOMPARE(state.extractionAnnouncementInterval, 5);
        QCOMPARE(state.extractionAnnouncementMode, QStringLiteral("both"));
    }

    void roundTripsBoundedSnapshot()
    {
        SteamUiState source;
        source.viewMode = QStringLiteral("chart");
        source.coachVisualEnabled = true;
        source.coachCueText = QStringLiteral("Steam nearly complete");
        source.coachCueSeverity = QStringLiteral("positive");
        source.coachCueActive = true;
        source.durationMilkDerived = true;
        source.extractionAnnouncementsEnabled = false;
        source.extractionAnnouncementInterval = 30;
        source.extractionAnnouncementMode = QStringLiteral("timed");

        SteamUiState parsed;
        QString error;
        QVERIFY2(SteamUiState::parse(source.toJson(), &parsed, &error), qPrintable(error));
        QCOMPARE(parsed, source);
        QCOMPARE(parsed.toJson(), source.toJson());
    }

    void rejectsMalformedPreferencesAndCue()
    {
        const QJsonObject valid = SteamUiState{}.toJson();
        SteamUiState parsed;
        const auto rejected = [&parsed](QJsonObject object) {
            return !SteamUiState::parse(object, &parsed);
        };

        QJsonObject bad = valid;
        bad.insert(QStringLiteral("viewMode"), QStringLiteral("timer-chart"));
        QVERIFY(rejected(bad));

        bad = valid;
        QJsonObject access = bad.value(QStringLiteral("accessibility")).toObject();
        access.insert(QStringLiteral("extractionAnnouncementInterval"), 4);
        bad.insert(QStringLiteral("accessibility"), access);
        QVERIFY(rejected(bad));

        bad = valid;
        access = bad.value(QStringLiteral("accessibility")).toObject();
        access.insert(QStringLiteral("extractionAnnouncementMode"), QStringLiteral("milestone"));
        bad.insert(QStringLiteral("accessibility"), access);
        QVERIFY(rejected(bad));

        bad = valid;
        QJsonObject coach = bad.value(QStringLiteral("coach")).toObject();
        coach.insert(QStringLiteral("cueSeverity"), QStringLiteral("info"));
        bad.insert(QStringLiteral("coach"), coach);
        QVERIFY(rejected(bad));

        coach.insert(QStringLiteral("cueActive"), true);
        coach.insert(QStringLiteral("cueText"), QString(513, QLatin1Char('x')));
        bad.insert(QStringLiteral("coach"), coach);
        QVERIFY(rejected(bad));

        bad = valid;
        bad.insert(QStringLiteral("unknown"), true);
        QVERIFY(rejected(bad));
    }
};

QTEST_GUILESS_MAIN(tst_SteamUiState)
#include "tst_steamuistate.moc"
