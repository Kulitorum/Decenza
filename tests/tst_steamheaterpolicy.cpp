#include <QtTest>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>

#include "controllers/steamheaterpolicy.h"
#include "core/settings.h"
#include "core/settings_brew.h"

// SteamHeaterPolicy is the single derivation of the steam-heater target.
//
// Two distinct defect shapes live here:
//
//  1. The resolution itself — the veto/permission table. Every combination that
//     shipped wrong at some point is a row below.
//
//  2. That no SECOND derivation exists. ProfileManager::uploadCurrentProfile()
//     carried a private copy of this rule that knew only two of its inputs, so a
//     profile upload re-sent steam = 0 and silently undid a recipe activation's
//     heater state. Centralising fixed it; the source scan is what stops it
//     growing back, because a re-derived copy compiles, passes every behavioural
//     test that does not happen to trigger it, and reads as perfectly reasonable
//     code at the call site.

class tst_SteamHeaterPolicy : public QObject {
    Q_OBJECT

private:
    // A Settings over an isolated store. Its constructor seeds the default
    // presets (Small, Large), so a case only has to add what it needs.
    //
    // Everything goes through the SettingsBrew API rather than writing the
    // preset array with a bare QSettings handle: SettingsBrew holds its OWN
    // QSettings, and a value written through a second handle is not visible to
    // the cached one — a fixture that did that silently selected a different
    // pitcher than it thought, and the veto cases failed against correct code.
    struct Fixture {
        Settings settings;
        SteamHeaterPolicy policy{&settings};

        SettingsBrew* brew() { return settings.brew(); }

        // The built-in "Heater off" entry. Nothing to create — every install has
        // exactly one, and it is addressed by a sentinel rather than a position.
        static constexpr int offPitcher() { return SettingsBrew::HeaterOffPitcherIndex; }

        void selectPitcher(int index) { brew()->setSelectedSteamCup(index); }

        void setRecipe(SteamHeaterPolicy::RecipeSteamIntent intent) {
            policy.setRecipeIntentProvider([intent] { return intent; });
        }
    };

    using Intent = SteamHeaterPolicy::RecipeSteamIntent;

private slots:
    void init()
    {
        QTest::failOnWarning();
        QSettings store;
        store.clear();
        store.sync();
    }

    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("DecentEspresso"));
        QCoreApplication::setOrganizationDomain(QStringLiteral("decentespresso.com"));
        QCoreApplication::setApplicationName(QStringLiteral("Decenza-tst-steamheaterpolicy"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }

    // --- permission -------------------------------------------------------

    void keepWarmOnPermitsTheHeater()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(true);
        f.brew()->setSteamTemperature(160.0);

        QVERIFY(f.policy.resolve().on);
        QCOMPARE(f.policy.commandedTemperatureC(), 160.0);
    }

    void keepWarmOffLeavesTheHeaterCold()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(false);

        QVERIFY(!f.policy.resolve().on);
        // 0 is what goes on the wire — the DE1 reads it as "heater off".
        QCOMPARE(f.policy.commandedTemperatureC(), 0.0);
    }

    // A parked milk recipe does NOT warm the boiler on its own. Users leave a
    // recipe selected as the machine's resting state between drinks, so
    // "a latte is active" is a stale signal for "milk is coming" — Let the
    // recipe decide grants its permission as an EVENT at shot start. An earlier
    // build granted it from the parked state and lit the boiler for a latte
    // selected hours before anyone wanted one.
    void aParkedMilkRecipeDoesNotWarmTheHeaterByItself()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(false);
        f.brew()->setLetRecipeDecide(true);
        f.setRecipe(Intent::WantsSteam);

        QVERIFY(!f.policy.resolve().on);
    }

    // ...and shot start is what turns it on. MainController grants this from
    // onEspressoCycleStarted().
    void shotStartPermissionWarmsAMilkRecipe()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(false);
        f.brew()->setLetRecipeDecide(true);
        f.brew()->setSteamTemperature(155.0);
        f.setRecipe(Intent::WantsSteam);

        f.policy.setEventPermission(true);
        QVERIFY(f.policy.resolve().on);
        QCOMPARE(f.policy.commandedTemperatureC(), 155.0);
    }

    // Event permission is the transient half of the model: revoking it (the
    // return to Idle) drops the heater, where a STATE permission would survive.
    void eventPermissionDoesNotSurviveItsRevocation()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(false);
        f.policy.setEventPermission(true);
        QVERIFY(f.policy.resolve().on);

        f.policy.setEventPermission(false);
        QVERIFY(!f.policy.resolve().on);
    }

    void statePermissionSurvivesTheEndOfASteamEvent()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(true);
        f.policy.setEventPermission(true);
        f.policy.setEventPermission(false);

        QVERIFY(f.policy.resolve().on);
    }

    // The provider is PULLED, so a recipe change is picked up with no explicit
    // notification — the property that makes the six m_activeRecipe mutation
    // sites in MainController safe.
    void theRecipeStateIsReadLiveNotCached()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(true);
        f.brew()->setLetRecipeDecide(true);
        Intent intent = Intent::NoRecipe;
        f.policy.setRecipeIntentProvider([&intent] { return intent; });

        QVERIFY(f.policy.resolve().on);
        intent = Intent::NoSteam;
        QVERIFY(!f.policy.resolve().on);
        intent = Intent::WantsSteam;
        QVERIFY(f.policy.resolve().on);
    }

    void noProviderMeansNoActiveRecipe()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(true);
        f.brew()->setLetRecipeDecide(true);

        QCOMPARE(f.policy.recipeIntent(), Intent::NoRecipe);
        // Not vetoed: no recipe is not the same as a recipe that steams nothing.
        QVERIFY(f.policy.resolve().on);
    }

    // --- veto -------------------------------------------------------------

    void anOffPitcherVetoesAPermittedHeater()
    {
        Fixture f;
        f.selectPitcher(Fixture::offPitcher());
        f.brew()->setKeepWarmWhenIdle(true);

        QVERIFY(!f.policy.resolve().on);
    }

    void anOffPitcherBeatsAnActiveMilkRecipe()
    {
        Fixture f;
        f.selectPitcher(Fixture::offPitcher());
        f.brew()->setKeepWarmWhenIdle(true);
        f.brew()->setLetRecipeDecide(true);
        f.setRecipe(Intent::WantsSteam);

        QVERIFY(!f.policy.resolve().on);
    }

    // The point of Let the recipe decide, and the thing today's single boolean
    // cannot express: an active espresso turns the boiler off for a user who
    // otherwise keeps it warm.
    void anActiveRecipeThatDoesNotSteamVetoesAKeepWarmHeater()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(true);
        f.brew()->setLetRecipeDecide(true);
        f.setRecipe(Intent::NoSteam);

        QVERIFY(!f.policy.resolve().on);
    }

    // ...and only when the user asked for it. With the setting off the recipe
    // has no say in either direction.
    void anActiveRecipeThatDoesNotSteamIsIgnoredWhenTheSettingIsOff()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(true);
        f.brew()->setLetRecipeDecide(false);
        f.setRecipe(Intent::NoSteam);

        QVERIFY(f.policy.resolve().on);
    }

    // A GHC steam press puts the DE1 into Steam in firmware and the app cannot
    // refuse it, so event permission outranks every veto — resolving "off" there
    // would make every readout a lie about a boiler that is actively heating.
    // The built-in entry carries no values, so the live settings are what run.
    void eventPermissionOutranksEveryVeto()
    {
        Fixture f;
        f.selectPitcher(Fixture::offPitcher());
        f.brew()->setKeepWarmWhenIdle(false);
        f.brew()->setLetRecipeDecide(true);
        f.brew()->setSteamDisabled(true);
        f.brew()->setSteamTemperature(151.0);
        f.setRecipe(Intent::NoSteam);

        f.policy.setEventPermission(true);
        QVERIFY(f.policy.resolve().on);
        QCOMPARE(f.policy.commandedTemperatureC(), 151.0);
    }

    void theTransientFlagVetoesAPermittedHeater()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(true);
        f.brew()->setSteamDisabled(true);

        QVERIFY(!f.policy.resolve().on);
    }

    // --- the transient veto is cleared, so it must be RE-ASSERTED ---------

    // Found on a live simulated DE1, not by a test: the app launched with a
    // persisted steamDisabled, pushed TargetSteamTemp=0, then cleared the flag
    // on the transition out of Disconnected — and never re-sent. The machine sat
    // commanded-off while every setting said warm, and the settings screen read
    // "Current: 0°C" rather than "Off", because the RESOLVED state was on and
    // only the measured temperature disagreed.
    //
    // The asymmetry is the whole defect: clearing the veto changes what resolve()
    // returns, and nothing about clearing it puts that answer on the wire.
    // MainController re-sends on the return from dormant; this asserts the half
    // that makes such a re-send necessary — that the answer really did change.
    void clearingTheTransientVetoChangesTheResolvedTarget()
    {
        Fixture f;
        f.selectPitcher(0);
        f.brew()->setKeepWarmWhenIdle(true);
        f.brew()->setSteamTemperature(160.0);

        f.brew()->setSteamDisabled(true);
        QVERIFY(!f.policy.resolve().on);
        QCOMPARE(f.policy.commandedTemperatureC(), 0.0);

        // What MainController's dormant branch does on sleep/disconnect.
        f.brew()->setSteamDisabled(false);
        QVERIFY(f.policy.resolve().on);
        QCOMPARE(f.policy.commandedTemperatureC(), 160.0);
    }

    // The same guard as a source scan, because the behavioural half above cannot
    // see whether anyone re-sends. Clearing the flag and re-asserting must stay
    // in one place: they were separated by ~15 lines and the gap was the bug.
    void clearingTheTransientVetoIsPairedWithAReSend()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR)
                   + QStringLiteral("/src/controllers/maincontroller.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(file.readAll());

        const qsizetype clearAt = text.indexOf(QStringLiteral("clearing temporary steamDisabled flag"));
        QVERIFY2(clearAt >= 0, "the dormant-phase steamDisabled clear has moved or gone");
        const qsizetype reassertAt = text.indexOf(QStringLiteral("wake-steam-reassert"));
        QVERIFY2(reassertAt > clearAt,
                 "the transient veto is cleared with no re-send after it: the machine "
                 "keeps the target it was last given, and every readout disagrees with it");
    }

    // --- the readouts -----------------------------------------------------

    // The steam readouts must show "Off" from the RESOLVED state, never from the
    // measured boiler temperature: a boiler switched off five minutes ago still
    // reports 130 °C, so a readout keyed on the measurement says the heater is
    // on for as long as it takes to cool. There is no behaviour to assert here —
    // the defect is a QML binding reading the wrong source — so this scans for
    // the shape, the same way the second-derivation guard below does.
    void steamReadoutsDoNotDeriveOffFromTheMeasuredTemperature()
    {
        // Each entry: the QML file, and the property whose value it renders.
        const QVector<QPair<QString, QString>> readouts = {
            {QStringLiteral("components/layout/items/SteamTemperatureItem.qml"),
             QStringLiteral("heaterOff")},
            {QStringLiteral("pages/SteamPage.qml"), QStringLiteral("steamHeaterOff")},
        };
        const QDir qmlRoot(QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/qml"));
        QVERIFY2(qmlRoot.exists(), qPrintable(qmlRoot.path()));

        for (const auto& readout : readouts) {
            QFile file(qmlRoot.filePath(readout.first));
            QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
                     qPrintable(readout.first));
            const QString text = QString::fromUtf8(file.readAll());

            // The off-state property must be declared from MainController's
            // resolved state and from nothing else.
            const QRegularExpression declaration(
                QStringLiteral(R"(property\s+bool\s+%1\s*:\s*([^\n]+))").arg(readout.second));
            const QRegularExpressionMatch match = declaration.match(text);
            QVERIFY2(match.hasMatch(),
                     qPrintable(readout.first + QStringLiteral(" no longer declares ")
                                + readout.second));
            const QString expression = match.captured(1);
            QVERIFY2(expression.contains(QStringLiteral("MainController.steamHeaterOn")),
                     qPrintable(readout.first + QStringLiteral(": ") + readout.second
                                + QStringLiteral(" must come from MainController.steamHeaterOn, got: ")
                                + expression));
            QVERIFY2(!expression.contains(QStringLiteral("steamTemperature")),
                     qPrintable(readout.first + QStringLiteral(": ") + readout.second
                                + QStringLiteral(" reads a temperature, not the resolved state: ")
                                + expression));
        }
    }

    // The wizard's two halves of the "Heater off" marker must stay paired.
    // buildSteamJson writing it while applySteamJson never reads it (or the
    // reverse) makes a saved recipe reopen as a different drink, and both are
    // QML functions with no unit-test harness — so assert the SHAPE in source,
    // which is the realistic regression: someone edits one half.
    void theWizardWritesAndReadsTheHeaterOffMarkerAsAPair()
    {
        QFile file(QStringLiteral(DECENZA_SOURCE_DIR)
                   + QStringLiteral("/qml/pages/RecipeWizardPage.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(file.readAll());

        QVERIFY2(text.contains(QStringLiteral("s.heaterOff = true")),
                 "buildSteamJson no longer writes the heaterOff marker");
        QVERIFY2(text.contains(QStringLiteral("fHeaterOff = !!s.heaterOff")),
                 "applySteamJson no longer reads the heaterOff marker back");

        // Mutually exclusive with the pitcher fields: the built-in carries no
        // values, so a block holding both describes a pitcher that is not used
        // and leaves activation resolving two contradictory answers.
        QVERIFY2(text.contains(QStringLiteral("if (fHeaterOff) {")),
                 "the marker and the pitcher fields are no longer exclusive");
    }

    // --- the guard --------------------------------------------------------

    // A second derivation is the defect that produced the field bug. It is not
    // detectable behaviourally from any one call site, so this scans for the
    // shape instead: a steam temperature chosen by testing keepSteamHeaterOn.
    void noSecondDerivationOfTheSteamTargetExists()
    {
        const QDir srcRoot(QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/src"));
        QVERIFY2(srcRoot.exists(), qPrintable(srcRoot.path()));

        // The one file allowed to read the setting for this purpose.
        const QString policyImpl =
            QDir::cleanPath(srcRoot.filePath(QStringLiteral("controllers/steamheaterpolicy.cpp")));

        QStringList offenders;
        QDirIterator it(srcRoot.path(), {QStringLiteral("*.cpp")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = QDir::cleanPath(it.next());
            if (path == policyImpl)
                continue;

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QString text = QString::fromUtf8(file.readAll());

            // Either setting appearing in an expression that picks a temperature
            // is the re-derivation shape. Reading them to display or publish is
            // fine; deciding a steam target from them is not.
            //
            // Keep this pattern pointed at the CURRENT accessor names. It used to
            // name keepSteamHeaterOn(), which the split renamed out of existence —
            // leaving a scan that matched nothing and passed forever, which is
            // indistinguishable from a scan that is working.
            static const QRegularExpression reDerivation(
                QStringLiteral(R"((keepWarmWhenIdle|letRecipeDecide)\(\)[^;\n]*\?[^;\n]*steamTemperature)"));
            if (reDerivation.match(text).hasMatch())
                offenders << srcRoot.relativeFilePath(path);
        }

        QVERIFY2(offenders.isEmpty(),
                 qPrintable(QStringLiteral(
                                "these files derive a steam target instead of asking "
                                "SteamHeaterPolicy: ")
                            + offenders.join(QStringLiteral(", "))));
    }
};

QTEST_MAIN(tst_SteamHeaterPolicy)
#include "tst_steamheaterpolicy.moc"
