#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "profile/profile.h"
#include "profile/profileframe.h"
#include "profile/profilejson.h"
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

// Immutable snapshot of the built-ins as they were BEFORE this format change,
// taken from `main`. Nothing may rewrite these files — that is the entire point.
//
// `resources/profiles` cannot answer the question that matters. Those files were
// themselves rewritten by tools/profile_sync --rewrite-format, so round-tripping
// them proves only that the serializer is a fixed point on its own output. It
// would stay green through a change that dropped a key from every shipped
// profile, because the "expected" side would have lost the key too. That is not
// a hypothetical: the first attempt at this change stripped `recipe` blocks from
// 8 built-ins and de1app's simple-editor keys from 58 more with a green suite.
//
// This corpus is the fixed reference point that makes the parity audit mean
// "no key disappeared" rather than "the output equals itself".
static const QString LEGACY_PROFILES_DIR =
    QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/tests/data/profiles_legacy");

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

    // ===== The real regression gate: parity against the PRE-change files =====

    // A data-driven test whose _data() yields no rows passes silently. Both
    // corpora are load-bearing, so assert they are actually populated rather
    // than trusting a green run that may have executed nothing at all.
    void profileCorporaAreNotEmpty() {
        QCOMPARE_GE(QDir(BUILTIN_PROFILES_DIR)
                        .entryList({QStringLiteral("*.json")}, QDir::Files).size(), 90);
        QCOMPARE_GE(QDir(LEGACY_PROFILES_DIR)
                        .entryList({QStringLiteral("*.json")}, QDir::Files).size(), 90);
    }

    void legacyProfiles_data() {
        QTest::addColumn<QString>("filePath");

        QDir dir(LEGACY_PROFILES_DIR);
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString& f : files)
            QTest::newRow(qPrintable(f)) << dir.absoluteFilePath(f);
    }

    // THE gate. Take a profile exactly as it shipped before this change, run it
    // through the new serializer, and assert nothing was dropped or altered.
    //
    // The change makes exactly two deliberate transformations to legacy files.
    // Both are allowed here, but ONLY against positive evidence that the intent
    // actually held — an unconditional exemption for `steps` or `recipe` would
    // reopen the same blind spot this corpus exists to close, just one level up.
    // Anything else that moves is a regression.
    void legacyProfilesRoundTripLoseNothing_data() { legacyProfiles_data(); }

    void legacyProfilesRoundTripLoseNothing() {
        QFETCH(QString, filePath);

        QFile file(filePath);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(filePath));
        const QJsonObject legacy = QJsonDocument::fromJson(file.readAll()).object();

        const Profile p = Profile::loadFromFile(filePath);
        QVERIFY2(p.isValid(), qPrintable(filePath));
        const QJsonObject produced = p.toJsonObject();

        QJsonObject legacyCmp = legacy;
        QJsonObject producedCmp = produced;

        // Transformation 1 — frame materialization. de1app's simple editors
        // (2a/2b) store their shot as scalar settings and ship `steps: []`.
        // reaprime hard-rejects an empty `steps`, so those profiles were
        // literally unreadable by it; we now generate the frames the scalars
        // describe. That is an addition, not a loss — but only count it as one
        // if frames genuinely appeared.
        const QString type = legacy.value(QStringLiteral("legacy_profile_type")).toString();
        const bool simpleEditor = (type == QStringLiteral("settings_2a")
                                   || type == QStringLiteral("settings_2b"));
        if (simpleEditor && legacy.value(QStringLiteral("steps")).toArray().isEmpty()) {
            QVERIFY2(!produced.value(QStringLiteral("steps")).toArray().isEmpty(),
                     qPrintable(QFileInfo(filePath).fileName()
                                + ": simple profile still has no steps — reaprime cannot read it"));
            for (const QString& k : {QStringLiteral("steps"),
                                     QStringLiteral("number_of_preinfuse_frames")}) {
                legacyCmp.remove(k);
                producedCmp.remove(k);
            }
        }

        // Transformation 2 — the Decenza-private `recipe` block is dropped and
        // its payload promoted to the canonical ecosystem keys. These blocks had
        // gone stale: several carried a `targetWeight` contradicting the
        // profile's own `target_weight` (turbo_shot 45 vs 39), and an
        // `editorType` contradicting the title and legacy_profile_type — and
        // editor type is derived from those, never stored. `dose` is the only
        // field the canonical keys did not already carry, so require it to have
        // landed in `recommended_dose` before excusing the drop.
        if (legacy.contains(QStringLiteral("recipe"))
            && !produced.contains(QStringLiteral("recipe"))) {
            const QJsonObject recipe = legacy.value(QStringLiteral("recipe")).toObject();
            if (recipe.contains(QStringLiteral("dose"))) {
                QCOMPARE(profileJsonToDouble(produced.value(QStringLiteral("recommended_dose"))),
                         profileJsonToDouble(recipe.value(QStringLiteral("dose"))));
            }
            legacyCmp.remove(QStringLiteral("recipe"));
        }

        const QStringList lost = Profile::jsonParityErrors(legacyCmp, producedCmp);
        QVERIFY2(lost.isEmpty(),
                 qPrintable(QFileInfo(filePath).fileName() + ": " + lost.join("; ")));
    }

    // And the migrated form must satisfy the strictest reader in the ecosystem,
    // so a user upgrading Decenza does not end up with profiles reaprime rejects.
    void legacyProfilesBecomeReaprimeReadable_data() { legacyProfiles_data(); }

    void legacyProfilesBecomeReaprimeReadable() {
        QFETCH(QString, filePath);

        const Profile p = Profile::loadFromFile(filePath);
        QVERIFY2(p.isValid(), qPrintable(filePath));

        const QStringList errors = Profile::reaprimeReadabilityErrors(p.toJsonObject());
        QVERIFY2(errors.isEmpty(),
                 qPrintable(QFileInfo(filePath).fileName() + ": " + errors.join(", ")));
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

    // A value the source left empty, replaced by a non-zero number, is drift and
    // not a default: the reader would brew something the file never specified.
    // This is the direction a "helpful" serializer fails in, and the plain
    // key-survived check cannot see it — the key is present in both.
    void parityCheckerFlagsFabricatedValues() {
        const QJsonObject empty{{"espresso_temperature", QStringLiteral("")}};
        QVERIFY(!Profile::jsonParityErrors(
                    empty, QJsonObject{{"espresso_temperature", QStringLiteral("93.00")}}).isEmpty());

        // Filling an empty with a ZERO is inert — that is a default, not a claim.
        QVERIFY(Profile::jsonParityErrors(
                    empty, QJsonObject{{"espresso_temperature", QStringLiteral("0.00")}}).isEmpty());

        // A key whose absence is meaningful must be reported even when the value
        // dropped was itself zero. "Inert" cannot simply mean "parses to 0":
        // fromJson defaults an absent target_weight to 36.0, so dropping an
        // explicit "0" does not round-trip to 0 — it round-trips to 36 g, which
        // is a different shot. Keys like this are listed in nonZeroDefaultKeys().
        const QStringList errs = Profile::jsonParityErrors(
            QJsonObject{{"target_weight", QStringLiteral("0")}}, QJsonObject{});
        QVERIFY2(!errs.isEmpty(), "a non-zero-default key must not be treated as inert");

        // Whereas a key that really does default to 0 stays inert when dropped.
        QVERIFY(Profile::jsonParityErrors(
                    QJsonObject{{"target_volume_count_start", QStringLiteral("0")}},
                    QJsonObject{}).isEmpty());
    }

    // ===== An unrecognised step key is refused, not silently dropped =====
    //
    // Preserving such a key would not have helped: we would still ignore it while
    // brewing and pour a different shot than the file describes. Refusing is the
    // only honest option, so the user files a bug instead of chasing a bad cup.

    void unknownStepKeyMakesProfileInvalid() {
        QJsonObject json = makeProfileJson();
        QJsonArray steps = json.value(QStringLiteral("steps")).toArray();
        QJsonObject step = steps.at(0).toObject();
        step[QStringLiteral("exit_on_refractometer")] = QStringLiteral("1.35");
        steps.replace(0, step);
        json[QStringLiteral("steps")] = steps;

        const Profile p = Profile::fromJson(QJsonDocument(json));

        QVERIFY2(!p.isValid(), "a profile with an unmodelled step key must not import");
        QCOMPARE(p.unsupportedStepKeys(), QStringList{QStringLiteral("exit_on_refractometer")});

        // The message has to be worth pasting into a bug report — a bare
        // "invalid profile" gives the user nothing to report.
        const QStringList errors = p.validationErrors();
        QVERIFY(!errors.filter(QStringLiteral("exit_on_refractometer")).isEmpty());
    }

    void unknownStepKeysAreDedupedAcrossSteps() {
        QJsonObject json = makeProfileJson();
        QJsonObject a = json.value(QStringLiteral("steps")).toArray().at(0).toObject();
        a[QStringLiteral("mystery")] = 1;
        QJsonObject b = a;
        b[QStringLiteral("other")] = 2;
        json[QStringLiteral("steps")] = QJsonArray{a, b};

        const Profile p = Profile::fromJson(QJsonDocument(json));
        // Reported once each and sorted, not once per offending step.
        QCOMPARE(p.unsupportedStepKeys(),
                 (QStringList{QStringLiteral("mystery"), QStringLiteral("other")}));
    }

    // The strictness is only safe because the real-world key set is small and
    // fully modelled. If this ever fails, the allowlist has drifted from what we
    // actually parse and real profiles are about to start being rejected.
    void everyShippedProfileUsesOnlyKnownStepKeys_data() { builtinProfiles_data(); }

    void everyShippedProfileUsesOnlyKnownStepKeys() {
        QFETCH(QString, filePath);

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject onDisk = QJsonDocument::fromJson(file.readAll()).object();

        QStringList unknown;
        const QJsonArray steps = onDisk.value(QStringLiteral("steps")).toArray();
        for (const QJsonValue& s : steps)
            unknown << ProfileFrame::unknownJsonKeys(s.toObject());

        QVERIFY2(unknown.isEmpty(),
                 qPrintable(QFileInfo(filePath).fileName() + " uses unmodelled step keys: "
                            + unknown.join(QStringLiteral(", "))));
    }

    void legacyProfilesUseOnlyKnownStepKeys_data() { legacyProfiles_data(); }

    void legacyProfilesUseOnlyKnownStepKeys() {
        QFETCH(QString, filePath);

        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject onDisk = QJsonDocument::fromJson(file.readAll()).object();

        QStringList unknown;
        const QJsonArray steps = onDisk.value(QStringLiteral("steps")).toArray();
        for (const QJsonValue& s : steps)
            unknown << ProfileFrame::unknownJsonKeys(s.toObject());

        QVERIFY2(unknown.isEmpty(),
                 qPrintable(QFileInfo(filePath).fileName() + " uses unmodelled step keys: "
                            + unknown.join(QStringLiteral(", "))));
    }

    // The Tcl import had the identical hole: fromTclList's if/else chain has no
    // else, so an unmodelled de1app key fell through silently. Same rule now
    // applies, and this holds the line against the real de1app corpus — if it
    // ever fails, .tcl imports are about to start being refused in the field.
    void de1appTclProfiles_data() {
        QTest::addColumn<QString>("filePath");
        QDir dir(QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/tests/data/de1app_profiles"));
        const QStringList files = dir.entryList({QStringLiteral("*.tcl")}, QDir::Files, QDir::Name);
        for (const QString& f : files)
            QTest::newRow(qPrintable(f)) << dir.absoluteFilePath(f);
    }

    void de1appTclProfilesUseOnlyKnownStepKeys_data() { de1appTclProfiles_data(); }

    void de1appTclProfilesUseOnlyKnownStepKeys() {
        QFETCH(QString, filePath);

        QFile file(filePath);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(filePath));
        const Profile p = Profile::loadFromTclString(QString::fromUtf8(file.readAll()));

        QVERIFY2(p.unsupportedStepKeys().isEmpty(),
                 qPrintable(QFileInfo(filePath).fileName() + " would now be refused: "
                            + p.unsupportedStepKeys().join(QStringLiteral(", "))));
    }

    void unknownTclStepKeyMakesProfileInvalid() {
        // A frame carrying a setting this build does not model. Everything else
        // is a normal de1app frame, so only the unknown key can be the cause.
        const QString tcl = QStringLiteral(
            "advanced_shot {{exit_if 1 flow 2.0 volume 100 transition fast "
            "temperature 93.0 name {preinfusion} pressure 1.0 sensor coffee "
            "pump pressure seconds 10 exit_on_refractometer 1.35}}\n");

        const Profile p = Profile::loadFromTclString(tcl);
        QCOMPARE(p.unsupportedStepKeys(), QStringList{QStringLiteral("exit_on_refractometer")});
        QVERIFY2(!p.isValid(), "a Tcl profile with an unmodelled frame key must not import");
        QVERIFY(!p.validationErrors().filter(QStringLiteral("exit_on_refractometer")).isEmpty());
    }

    // Our own output must survive our own strictness — otherwise every save
    // would produce a file the next launch refuses to load.
    void canonicalSerializationRoundTripsThroughStrictImport() {
        const Profile p = Profile::loadFromFile(
            QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/resources/profiles/d_flow_default.json"));
        QVERIFY(p.isValid());

        const Profile again = Profile::fromJson(QJsonDocument(p.toJsonObject()));
        QVERIFY2(again.isValid(),
                 qPrintable(QStringLiteral("our own output was rejected: ")
                            + again.unsupportedStepKeys().join(QStringLiteral(", "))));
    }

    // ===== The precision policy is the format's contract with the editors =====

    void canonicalEncodingPreservesEditorSteps_data() {
        QTest::addColumn<double>("value");
        QTest::addColumn<int>("decimals");
        QTest::addColumn<QString>("encoded");

        // Every row is a value a shipped editor control can actually produce.
        // If the encoding cannot hold it, a save/reload silently changes the shot
        // — which is exactly how 36.5 g became 37 g.
        QTest::newRow("target weight 0.1 g step") << 36.5 << ProfileJson::TargetMass  << "36.5";
        QTest::newRow("limiter 0.01 step")        << 0.05 << ProfileJson::Limiter     << "0.05";
        QTest::newRow("pressure 0.01 step")       << 8.75 << ProfileJson::Pressure    << "8.75";
        QTest::newRow("flow 0.01 step")           << 2.35 << ProfileJson::Flow        << "2.35";
        QTest::newRow("temperature 0.1 step")     << 92.4 << ProfileJson::Temperature << "92.40";
        QTest::newRow("tank temp 1 C step")       << 60.0 << ProfileJson::TankTemp    << "60.0";
        // Not our editor's step but an imported de1app frame's: toTclList() writes
        // volume with 1dp, so 22.5 mL must not truncate just because our own
        // control is integer-only. The two writers have to agree.
        QTest::newRow("imported volume 22.5 mL")  << 22.5 << ProfileJson::Volume      << "22.5";
    }

    void canonicalEncodingPreservesEditorSteps() {
        QFETCH(double, value);
        QFETCH(int, decimals);
        QFETCH(QString, encoded);

        const QString out = ProfileJson::enc(value, decimals);
        QCOMPARE(out, encoded);
        // And it must read back as the same number, not merely look right.
        QCOMPARE(profileJsonToDouble(QJsonValue(out)), value);
    }

    void canonicalEncodingClampsNegativeZero() {
        // A de1app cleaning profile carries -5.7e-15, which formats as "-0.00" —
        // valid to any parser, but it reappears in every diff and on every
        // re-import unless the encoder fixes it rather than the data file.
        QCOMPARE(ProfileJson::enc(-5.7e-15, ProfileJson::Pressure), QStringLiteral("0.00"));
        QCOMPARE(ProfileJson::enc(-0.0, ProfileJson::Flow), QStringLiteral("0.00"));
        // A genuinely negative value is left alone.
        QCOMPARE(ProfileJson::enc(-1.25, ProfileJson::Pressure), QStringLiteral("-1.25"));
    }

    // A LIVE upload carries the canonical serialization — the same bytes that go
    // to disk, to an export and into a share code.
    //
    // The previous version of this test fed p.toJsonObject() in as the stored
    // snapshot and then asserted the output equalled p.toJsonObject(). Since the
    // history path passes the snapshot through verbatim, that compared a value
    // with itself: it would have passed with any serializer at all, canonical or
    // not. The two paths are now tested separately, for the property each
    // actually has.
    void visualizerLiveUploadProfileIsCanonical() {
        const Profile p = Profile::loadFromFile(
            QStringLiteral(DECENZA_SOURCE_DIR) + QStringLiteral("/resources/profiles/d_flow_default.json"));
        QVERIFY(p.isValid());

        const QJsonObject uploaded = VisualizerUploader::buildVisualizerProfileJson(&p);

        QVERIFY2(!uploaded.isEmpty(), "live upload carried no profile object");
        QCOMPARE(QJsonDocument(uploaded).toJson(QJsonDocument::Compact),
                 QJsonDocument(p.toJsonObject()).toJson(QJsonDocument::Compact));

        const QStringList errors = Profile::reaprimeReadabilityErrors(uploaded);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(", ")));
    }

    void visualizerLiveUploadHandlesNullProfile() {
        // Shots can be recorded with no profile attached; the uploader must
        // still produce a well-formed object rather than an empty one.
        const QJsonObject uploaded = VisualizerUploader::buildVisualizerProfileJson(nullptr);
        QVERIFY(!uploaded.isEmpty());
        QVERIFY(uploaded.contains(QStringLiteral("title")));
    }

    // A HISTORY upload re-sends the profile snapshot stored WITH the shot,
    // byte-for-byte. Re-serializing an old shot through today's serializer would
    // rewrite what the user actually brewed, so this deliberately feeds a
    // NON-canonical snapshot (numeric encoding, a key we no longer emit) and
    // requires it back unchanged. This fails the moment anyone reintroduces
    // re-serialization on the history path.
    void historyUploadPreservesStoredProfileVerbatim() {
        const QJsonObject stale{
            {"title", "Ancient Shot"},
            {"legacy_profile_type", "settings_2c"},
            {"target_weight", 36.5},                       // number, not the canonical string
            {"espresso_temperature", 91.5},                // number, not the canonical string
            {"a_key_we_no_longer_emit", "keep me"},
            {"steps", QJsonArray{QJsonObject{{"name", "pour"}, {"seconds", 25}}}},
        };
        const QByteArray staleBytes = QJsonDocument(stale).toJson(QJsonDocument::Compact);

        ShotProjection proj;
        proj.pressure = QVariantList{};
        proj.profileJson = QString::fromUtf8(staleBytes);

        const QByteArray payload = VisualizerUploader::buildHistoryShotJson(proj);
        const QJsonObject uploaded = QJsonDocument::fromJson(payload).object()["profile"].toObject();

        QCOMPARE(QJsonDocument(uploaded).toJson(QJsonDocument::Compact), staleBytes);
    }
};

QTEST_MAIN(tst_BuiltinProfileFormat)
#include "tst_builtinprofileformat.moc"
