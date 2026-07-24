#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "profile/profile.h"
#include "network/visualizeruploader.h"
#include "history/shotprojection.h"

// Contract test for the canonical profile JSON format
// (OpenSpec: align-profile-json-with-reaprime).
//
// The Decent community needs a profile to make the same coffee in every app, so
// every profile Decenza emits must be readable by the strictest reader in the
// ecosystem — reaprime's Profile.fromJson, which hard-rejects a profile missing
// `tank_temperature` / `target_volume_count_start` or carrying an empty `steps`
// array. This runs the shipped built-ins through that contract, both as they sit
// on disk and after a load→serialize cycle.

static const QString BUILTIN_PROFILES_DIR =
    QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/resources/profiles");

class tst_BuiltinProfileFormat : public QObject {
    Q_OBJECT

private:
    // A minimal valid advanced profile, in the de1app-style string encoding a
    // foreign app would hand us.
    static QJsonObject makeProfileJson() {
        QJsonObject step{
            {"name", "preinfusion"}, {"pump", "flow"}, {"sensor", "coffee"},
            {"transition", "fast"}, {"temperature", QStringLiteral("93.0")},
            {"pressure", QStringLiteral("1.0")}, {"flow", QStringLiteral("4.0")},
            {"seconds", QStringLiteral("20.0")}, {"volume", QStringLiteral("0")},
        };
        return QJsonObject{
            {"title", "Parity Test"}, {"legacy_profile_type", "settings_2c"},
            {"version", "2"}, {"beverage_type", "espresso"},
            {"target_weight", QStringLiteral("36.0")},
            {"steps", QJsonArray{step}},
        };
    }

private slots:
    void init() { QTest::failOnWarning(); }

    void builtinProfiles_data() {
        QTest::addColumn<QString>("filePath");

        QDir dir(BUILTIN_PROFILES_DIR);
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString& f : files)
            QTest::newRow(qPrintable(f)) << dir.absoluteFilePath(f);
    }

    // Every shipped built-in, as it sits on disk, satisfies reaprime's contract.
    void builtinProfiles() {
        QFETCH(QString, filePath);

        QFile file(filePath);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(filePath));
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
        QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));

        const QStringList errors = Profile::reaprimeReadabilityErrors(doc.object());
        QVERIFY2(errors.isEmpty(), qPrintable(QFileInfo(filePath).fileName() + ": " + errors.join(", ")));
    }

    // A built-in that survives a load → canonical-serialize cycle is still
    // readable, and the serialized form is what an export/share would emit.
    void builtinProfilesRoundTripStayReadable_data() { builtinProfiles_data(); }

    void builtinProfilesRoundTripStayReadable() {
        QFETCH(QString, filePath);

        const Profile p = Profile::loadFromFile(filePath);
        QVERIFY2(p.isValid(), qPrintable(filePath));

        const QJsonObject out = p.toJsonObject();
        const QStringList errors = Profile::reaprimeReadabilityErrors(out);
        QVERIFY2(errors.isEmpty(), qPrintable(QFileInfo(filePath).fileName() + ": " + errors.join(", ")));

        // Canonical format: string-encoded values and the required aliases.
        QVERIFY(out.value("target_weight").isString());
        QCOMPARE(out.value("tank_temperature"), out.value("tank_desired_water_temperature"));
        QCOMPARE(out.value("target_volume_count_start"), out.value("number_of_preinfuse_frames"));
    }

    // ===== Parity audit: a format change must never lose data =====
    //
    // This is the guard the first attempt at this change lacked. The whole suite
    // passed while the serializer was silently stripping `recipe` blocks from 8
    // built-ins and de1app's simple-editor keys from 58 more, because every test
    // asserted what the OUTPUT looks like and none asserted that the INPUT survived.

    void builtinProfilesRoundTripLosesNothing_data() { builtinProfiles_data(); }

    void builtinProfilesRoundTripLosesNothing() {
        QFETCH(QString, filePath);

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject onDisk = QJsonDocument::fromJson(file.readAll()).object();

        const Profile p = Profile::loadFromFile(filePath);
        QVERIFY(p.isValid());

        // Every key and value in the shipped file must survive load -> serialize.
        const QStringList lost = Profile::jsonParityErrors(onDisk, p.toJsonObject());
        QVERIFY2(lost.isEmpty(),
                 qPrintable(QFileInfo(filePath).fileName() + ": " + lost.join("; ")));
    }

    void builtinProfilesSerializationIsIdempotent_data() { builtinProfiles_data(); }

    void builtinProfilesSerializationIsIdempotent() {
        QFETCH(QString, filePath);

        // serialize(parse(serialize(p))) must equal serialize(p). A serializer that
        // is not a fixed point means every save mutates the file a little further.
        const QJsonObject once = Profile::loadFromFile(filePath).toJsonObject();
        const QJsonObject twice =
            Profile::fromJson(QJsonDocument(once)).toJsonObject();
        QCOMPARE(QJsonDocument(twice).toJson(QJsonDocument::Compact),
                 QJsonDocument(once).toJson(QJsonDocument::Compact));
    }

    // Serialization precision must not fall below what the editors can set, or a
    // save/reload silently changes the shot. ProfileEditorPage uses 0.1 g steps for
    // target weight and 0.01 steps for limiter ranges; serializing those with too
    // few decimals turned 36.5 g into 37 g and a 0.05 limiter range into 0.1.
    void editorResolutionSurvivesRoundTrip() {
        Profile p = Profile::fromJson(QJsonDocument(makeProfileJson()));
        p.setTargetWeight(36.5);

        QList<ProfileFrame> steps = p.steps();
        QVERIFY(!steps.isEmpty());
        steps[0].maxFlowOrPressure = 6.25;
        steps[0].maxFlowOrPressureRange = 0.05;
        p.setSteps(steps);

        const Profile reloaded = Profile::fromJson(QJsonDocument(p.toJsonObject()));
        QCOMPARE(reloaded.targetWeight(), 36.5);
        QCOMPARE(reloaded.steps()[0].maxFlowOrPressure, 6.25);
        QCOMPARE(reloaded.steps()[0].maxFlowOrPressureRange, 0.05);
    }

    // A profile carrying keys Decenza does not model must keep them, so a profile
    // authored in de1app/reaprime survives a Decenza load->save round trip.
    void unmodelledKeysSurviveRoundTrip() {
        QJsonObject src = makeProfileJson();
        src["flow_profile_minimum_pressure"] = QStringLiteral("4.0");
        src["some_future_app_key"] = QStringLiteral("keep me");

        const Profile p = Profile::fromJson(QJsonDocument(src));
        const QJsonObject out = p.toJsonObject();
        QCOMPARE(out.value("flow_profile_minimum_pressure").toString(), QStringLiteral("4.0"));
        QCOMPARE(out.value("some_future_app_key").toString(), QStringLiteral("keep me"));
        QVERIFY(Profile::jsonParityErrors(src, out).isEmpty());
    }

    // The parity checker must actually catch the two failure modes it exists for,
    // or it is decoration. Guards the guard.
    void parityCheckerDetectsLossAndDrift() {
        QJsonObject before;
        before["recipe"] = QJsonObject{{"dose", 18}};
        before["espresso_pressure"] = QStringLiteral("9.0");
        before["target_weight"] = QStringLiteral("36.5");
        before["inert_zero"] = QStringLiteral("0.0");

        // Dropped object + dropped non-zero scalar + drifted value.
        QJsonObject after;
        after["target_weight"] = QStringLiteral("37.0");

        const QStringList errs = Profile::jsonParityErrors(before, after);
        QVERIFY2(errs.size() == 3, qPrintable(errs.join("; ")));
        QVERIFY(errs.filter("recipe").size() == 1);          // object lost
        QVERIFY(errs.filter("espresso_pressure").size() == 1); // non-zero scalar lost
        QVERIFY(errs.filter("target_weight").size() == 1);   // value drifted
        // A dropped zero is inert and must NOT be reported.
        QVERIFY(errs.filter("inert_zero").isEmpty());
        // Encoding-only differences are not drift.
        QVERIFY(Profile::jsonParityErrors(
                    QJsonObject{{"p", 9.0}}, QJsonObject{{"p", QStringLiteral("9.00")}}).isEmpty());
    }

    // The Visualizer upload carries the SAME canonical serialization as the
    // on-disk/exported profile — one format everywhere. Guards the delegation in
    // buildVisualizerProfileJson()/buildHistoryShotJson(): if either ever goes back
    // to hand-building its own payload, this diverges and fails.
    void visualizerUploadProfileIsCanonical() {
        const Profile p = Profile::loadFromFile(
            QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/resources/profiles/d_flow_default.json"));
        QVERIFY(p.isValid());

        ShotProjection proj;
        proj.pressure = QVariantList{};
        proj.profileJson = QString::fromUtf8(QJsonDocument(p.toJsonObject()).toJson(QJsonDocument::Compact));

        const QByteArray payload = VisualizerUploader::buildHistoryShotJson(proj);
        const QJsonObject uploaded = QJsonDocument::fromJson(payload).object()["profile"].toObject();

        QVERIFY2(!uploaded.isEmpty(), "upload payload carried no profile object");
        // Byte-identical to the canonical serialization of the same profile.
        QCOMPARE(QJsonDocument(uploaded).toJson(QJsonDocument::Compact),
                 QJsonDocument(p.toJsonObject()).toJson(QJsonDocument::Compact));
        // And it satisfies reaprime's reader contract.
        const QStringList errors = Profile::reaprimeReadabilityErrors(uploaded);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(", ")));
    }
};

QTEST_MAIN(tst_BuiltinProfileFormat)
#include "tst_builtinprofileformat.moc"
