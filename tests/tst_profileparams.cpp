#include <QtTest>
#include <QJsonObject>
#include <QVariantMap>

#include "profile/profileparams.h"
#include "profile/profile.h"

// Test ProfileParams serialization, validation, clamping, and frameAffectingFieldsEqual.
// Expected defaults from de1app D-Flow/A-Flow stock profiles.
// ProfileParams is a plain struct — no friend access needed.

class tst_ProfileParams : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    // ==========================================
    // JSON round-trip
    // ==========================================

    void jsonRoundTrip() {
        ProfileParams original;
        original.targetWeight = 42.0;
        original.targetVolume = 100.0;
        original.fillTemperature = 85.0;
        original.infusePressure = 2.5;
        original.infuseTime = 15.0;
        original.infuseWeight = 3.0;
        original.infuseVolume = 80.0;
        original.pourTemperature = 90.0;
        original.pourPressure = 8.0;
        original.pourFlow = 2.5;
        original.rampTime = 8.0;
        original.rampDownEnabled = true;
        original.flowExtractionUp = false;
        original.secondFillEnabled = true;
        original.editorType = EditorType::AFlow;
        original.preinfuseFrameCount = 3;

        QJsonObject json = original.toJson();
        ProfileParams parsed = ProfileParams::fromJson(json);

        QCOMPARE(parsed.targetWeight, 42.0);
        QCOMPARE(parsed.targetVolume, 100.0);
        QCOMPARE(parsed.fillTemperature, 85.0);
        QCOMPARE(parsed.infusePressure, 2.5);
        QCOMPARE(parsed.pourTemperature, 90.0);
        QCOMPARE(parsed.pourPressure, 8.0);
        QCOMPARE(parsed.pourFlow, 2.5);
        QCOMPARE(parsed.rampTime, 8.0);
        QVERIFY(parsed.rampDownEnabled);
        QVERIFY(!parsed.flowExtractionUp);
        QVERIFY(parsed.secondFillEnabled);
        // editorType is intentionally not serialized to JSON (derived from profile title at load time)
        QCOMPARE(parsed.editorType, EditorType::DFlow);  // default when absent from JSON
        QCOMPARE(parsed.preinfuseFrameCount, 3);
    }

    void variantMapRoundTrip() {
        ProfileParams original;
        original.targetWeight = 38.0;
        original.pourFlow = 3.0;
        original.editorType = EditorType::Pressure;

        QVariantMap map = original.toVariantMap();
        ProfileParams parsed = ProfileParams::fromVariantMap(map);

        QCOMPARE(parsed.targetWeight, 38.0);
        QCOMPARE(parsed.pourFlow, 3.0);
        QCOMPARE(parsed.editorType, EditorType::Pressure);
    }

    void jsonMissingFieldsUseDefaults() {
        ProfileParams parsed = ProfileParams::fromJson(QJsonObject());

        // Verify key defaults
        QCOMPARE(parsed.targetWeight, 36.0);
        QCOMPARE(parsed.fillTemperature, 88.0);
        QCOMPARE(parsed.editorType, EditorType::DFlow);
        QCOMPARE(parsed.preinfuseFrameCount, -1);  // Sentinel: use countPreinfuseFrames()
    }

    // ==========================================
    // Editor type string conversion
    // ==========================================

    void editorTypeRoundTrip_data() {
        QTest::addColumn<int>("editorType");
        QTest::addColumn<QString>("expectedString");

        QTest::newRow("DFlow")    << int(EditorType::DFlow)    << "dflow";
        QTest::newRow("AFlow")    << int(EditorType::AFlow)    << "aflow";
        QTest::newRow("Pressure") << int(EditorType::Pressure) << "pressure";
        QTest::newRow("Flow")     << int(EditorType::Flow)     << "flow";
    }

    void editorTypeRoundTrip() {
        QFETCH(int, editorType);
        QFETCH(QString, expectedString);

        EditorType et = static_cast<EditorType>(editorType);
        QCOMPARE(editorTypeToString(et), expectedString);
        QCOMPARE(static_cast<int>(editorTypeFromString(expectedString)), editorType);
    }

    void editorTypeUnknownFallback() {
        QCOMPARE(editorTypeFromString("unknown"), EditorType::DFlow);
        QCOMPARE(editorTypeFromString(""), EditorType::DFlow);
    }

    // ==========================================
    // applyEditorDefaults (de1app stock profile values)
    // ==========================================

    void applyEditorDefaultsDFlow() {
        // Values from D_Flow____default.tcl stock profile (de1app)
        ProfileParams params;
        params.editorType = EditorType::DFlow;
        params.applyEditorDefaults();

        QCOMPARE(params.fillTemperature, 88.0);
        QCOMPARE(params.infuseTime, 60.0);
        QCOMPARE(params.infusePressure, 3.0);
        QCOMPARE(params.infuseWeight, 4.0);
        QCOMPARE(params.pourTemperature, 88.0);
        QCOMPARE(params.pourPressure, 8.5);
        QCOMPARE(params.pourFlow, 1.7);
        QCOMPARE(params.targetWeight, 50.0);
        QCOMPARE(params.preinfuseFrameCount, 2);
    }

    void applyEditorDefaultsAFlow() {
        // Values from A-Flow____default-medium.tcl stock profile (de1app)
        ProfileParams params;
        params.editorType = EditorType::AFlow;
        params.applyEditorDefaults();

        QCOMPARE(params.fillTemperature, 95.0);
        QCOMPARE(params.infuseTime, 60.0);
        QCOMPARE(params.infuseWeight, 3.6);
        QCOMPARE(params.pourTemperature, 95.0);
        QCOMPARE(params.pourPressure, 10.0);
        QCOMPARE(params.pourFlow, 2.0);
        QCOMPARE(params.rampTime, 10.0);
        QCOMPARE(params.targetWeight, 36.0);
        QCOMPARE(params.preinfuseFrameCount, 2);
    }

    void applyEditorDefaultsSimpleNoOp() {
        // Pressure/Flow editors use struct defaults, applyEditorDefaults is a no-op
        ProfileParams params;
        params.editorType = EditorType::Pressure;
        double originalHoldTime = params.holdTime;
        params.applyEditorDefaults();
        QCOMPARE(params.holdTime, originalHoldTime);  // Unchanged
    }

    // ==========================================
    // Clamping
    // ==========================================

    void clampPressureRange() {
        ProfileParams params;
        params.infusePressure = 20.0;  // Over 12 bar max
        params.espressoPressure = -5.0;  // Negative
        params.clamp();
        QCOMPARE(params.infusePressure, 12.0);
        QCOMPARE(params.espressoPressure, 0.0);
    }

    void clampFlowRange() {
        ProfileParams params;
        // The ceiling admits a higher-flow machine's profiles so they survive a round
        // trip through the editor unclamped — see Profile::kMaxSettableFlow.
        params.pourFlow = 15.0;
        params.holdFlow = -1.0;
        params.clamp();
        QCOMPARE(params.pourFlow, 15.0);
        QCOMPARE(params.holdFlow, 0.0);

        params.pourFlow = 25.0;  // Over the ceiling
        params.clamp();
        QCOMPARE(params.pourFlow, Profile::kMaxSettableFlow);
    }

    void clampTemperatureRange() {
        ProfileParams params;
        params.fillTemperature = 200.0;  // Over 110C max
        params.pourTemperature = -10.0;
        params.clamp();
        QCOMPARE(params.fillTemperature, 110.0);
        QCOMPARE(params.pourTemperature, 0.0);
    }

    void clampNegativeTimesToZero() {
        ProfileParams params;
        params.infuseTime = -5.0;
        params.holdTime = -1.0;
        params.clamp();
        QCOMPARE(params.infuseTime, 0.0);
        QCOMPARE(params.holdTime, 0.0);
    }

    // ==========================================
    // Validation
    // ==========================================

    void validateNormalValuesPass() {
        ProfileParams params;  // All defaults are valid
        QVERIFY(params.validate().isEmpty());
    }

    void validateOutOfRangeReportsErrors() {
        ProfileParams params;
        params.targetWeight = -10.0;
        params.infusePressure = 15.0;
        // Above kMaxSettableFlow, not merely above the old 10 — 20 is now the legal
        // ceiling, so the previous value here would assert that a valid profile is invalid.
        params.pourFlow = Profile::kMaxSettableFlow + 5.0;
        params.infuseTime = -1.0;
        params.preinfuseFrameCount = 25;

        QStringList issues = params.validate();
        QVERIFY(issues.size() >= 5);
    }

    void clampProducesValuesValidateAccepts() {
        // The two carried separate copies of the same ceilings and drifted: clamp() and
        // the editors were widened to 20 while validate() stayed at 10/12, so a legally
        // authored high-flow profile logged "out of range" on every save. One assertion
        // ties them together.
        ProfileParams params;
        params.pourFlow = 99.0;
        params.holdFlow = 99.0;
        params.flowEnd = 99.0;
        params.preinfusionFlowRate = 99.0;
        params.limiterValue = 99.0;
        params.limiterRange = 99.0;
        params.espressoPressure = 99.0;
        params.targetWeight = 9999.0;
        params.clamp();

        const QStringList issues = params.validate();
        QVERIFY2(issues.isEmpty(), qPrintable(issues.join("; ")));
    }

    void validateSentinelPreinfuseFrameCount() {
        ProfileParams params;
        params.preinfuseFrameCount = -1;  // Valid sentinel
        QStringList issues = params.validate();
        // -1 should NOT trigger an error
        for (const QString& issue : issues) {
            QVERIFY(!issue.contains("preinfuseFrameCount"));
        }
    }

    // ==========================================
    // frameAffectingFieldsEqual
    // ==========================================

    void frameFieldsEqualWhenOnlyWeightDiffers() {
        // Weight doesn't affect frames — should still be equal
        ProfileParams a, b;
        b.targetWeight = a.targetWeight + 10.0;
        QVERIFY(a.frameAffectingFieldsEqual(b));
    }

    // No dose case here any more: ProfileParams has no `dose`. The per-profile dose
    // is Profile::recommendedDose, which is not part of this comparison at all —
    // see tst_profile's promotion cases.

    void frameFieldsEqualWhenOnlyVolumeDiffers() {
        ProfileParams a, b;
        b.targetVolume = a.targetVolume + 50.0;
        QVERIFY(a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenFlowDiffers() {
        ProfileParams a, b;
        b.pourFlow = a.pourFlow + 1.0;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenPressureDiffers() {
        ProfileParams a, b;
        b.infusePressure = a.infusePressure + 1.0;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenTimeDiffers() {
        ProfileParams a, b;
        b.holdTime = a.holdTime + 5.0;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenTempDiffers() {
        ProfileParams a, b;
        b.pourTemperature = a.pourTemperature + 2.0;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenEditorTypeDiffers() {
        ProfileParams a, b;
        a.editorType = EditorType::DFlow;
        b.editorType = EditorType::AFlow;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenBoolDiffers() {
        ProfileParams a, b;
        b.rampDownEnabled = !a.rampDownEnabled;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    // ==========================================
    // Legacy migration: pourStyle field
    // ==========================================

    void legacyPourStylePressure() {
        // Old format had pourStyle="pressure" + flowLimit
        QJsonObject json;
        json["pourStyle"] = "pressure";
        json["pourPressure"] = 9.0;
        json["pourFlow"] = 2.0;
        json["flowLimit"] = 3.5;

        ProfileParams params = ProfileParams::fromJson(json);
        QCOMPARE(params.pourPressure, 9.0);
        QCOMPARE(params.pourFlow, 3.5);  // flowLimit replaces pourFlow
    }

    void legacyPourStyleFlow() {
        QJsonObject json;
        json["pourStyle"] = "flow";
        json["pourPressure"] = 9.0;
        json["pourFlow"] = 2.0;
        json["pressureLimit"] = 6.0;

        ProfileParams params = ProfileParams::fromJson(json);
        QCOMPARE(params.pourFlow, 2.0);
        QCOMPARE(params.pourPressure, 6.0);  // pressureLimit replaces pourPressure
    }
};

QTEST_GUILESS_MAIN(tst_ProfileParams)
#include "tst_profileparams.moc"
