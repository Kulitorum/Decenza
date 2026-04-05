#include <QtTest>
#include <QJsonObject>
#include <QVariantMap>

#include "profile/recipeparams.h"

// Test RecipeParams serialization, validation, clamping, and frameAffectingFieldsEqual.
// Expected defaults from de1app D-Flow/A-Flow stock profiles.
// RecipeParams is a plain struct — no friend access needed.

class tst_RecipeParams : public QObject {
    Q_OBJECT

private slots:

    // ==========================================
    // JSON round-trip
    // ==========================================

    void jsonRoundTrip() {
        RecipeParams original;
        original.targetWeight = 42.0;
        original.targetVolume = 100.0;
        original.dose = 20.0;
        original.fillTemperature = 85.0;
        original.fillPressure = 4.0;
        original.fillFlow = 7.0;
        original.fillTimeout = 20.0;
        original.infuseEnabled = false;
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
        RecipeParams parsed = RecipeParams::fromJson(json);

        QCOMPARE(parsed.targetWeight, 42.0);
        QCOMPARE(parsed.targetVolume, 100.0);
        QCOMPARE(parsed.dose, 20.0);
        QCOMPARE(parsed.fillTemperature, 85.0);
        QCOMPARE(parsed.fillPressure, 4.0);
        QCOMPARE(parsed.fillFlow, 7.0);
        QCOMPARE(parsed.fillTimeout, 20.0);
        QVERIFY(!parsed.infuseEnabled);
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
        RecipeParams original;
        original.targetWeight = 38.0;
        original.pourFlow = 3.0;
        original.editorType = EditorType::Pressure;

        QVariantMap map = original.toVariantMap();
        RecipeParams parsed = RecipeParams::fromVariantMap(map);

        QCOMPARE(parsed.targetWeight, 38.0);
        QCOMPARE(parsed.pourFlow, 3.0);
        QCOMPARE(parsed.editorType, EditorType::Pressure);
    }

    void jsonMissingFieldsUseDefaults() {
        RecipeParams parsed = RecipeParams::fromJson(QJsonObject());

        // Verify key defaults
        QCOMPARE(parsed.targetWeight, 36.0);
        QCOMPARE(parsed.dose, 18.0);
        QCOMPARE(parsed.fillTemperature, 88.0);
        QCOMPARE(parsed.fillPressure, 3.0);
        QVERIFY(parsed.infuseEnabled);
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
        RecipeParams params;
        params.editorType = EditorType::DFlow;
        params.applyEditorDefaults();

        QCOMPARE(params.fillTemperature, 88.0);
        QCOMPARE(params.fillPressure, 3.0);
        QCOMPARE(params.fillTimeout, 25.0);
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
        RecipeParams params;
        params.editorType = EditorType::AFlow;
        params.applyEditorDefaults();

        QCOMPARE(params.fillTemperature, 95.0);
        QCOMPARE(params.fillPressure, 3.0);
        QCOMPARE(params.fillTimeout, 15.0);
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
        RecipeParams params;
        params.editorType = EditorType::Pressure;
        double originalHoldTime = params.holdTime;
        params.applyEditorDefaults();
        QCOMPARE(params.holdTime, originalHoldTime);  // Unchanged
    }

    // ==========================================
    // Clamping
    // ==========================================

    void clampPressureRange() {
        RecipeParams params;
        params.fillPressure = 20.0;  // Over 12 bar max
        params.espressoPressure = -5.0;  // Negative
        params.clamp();
        QCOMPARE(params.fillPressure, 12.0);
        QCOMPARE(params.espressoPressure, 0.0);
    }

    void clampFlowRange() {
        RecipeParams params;
        params.pourFlow = 15.0;  // Over 10 mL/s max
        params.holdFlow = -1.0;
        params.clamp();
        QCOMPARE(params.pourFlow, 10.0);
        QCOMPARE(params.holdFlow, 0.0);
    }

    void clampTemperatureRange() {
        RecipeParams params;
        params.fillTemperature = 200.0;  // Over 110C max
        params.pourTemperature = -10.0;
        params.clamp();
        QCOMPARE(params.fillTemperature, 110.0);
        QCOMPARE(params.pourTemperature, 0.0);
    }

    void clampNegativeTimesToZero() {
        RecipeParams params;
        params.fillTimeout = -5.0;
        params.holdTime = -1.0;
        params.clamp();
        QCOMPARE(params.fillTimeout, 0.0);
        QCOMPARE(params.holdTime, 0.0);
    }

    // ==========================================
    // Validation
    // ==========================================

    void validateNormalValuesPass() {
        RecipeParams params;  // All defaults are valid
        QVERIFY(params.validate().isEmpty());
    }

    void validateOutOfRangeReportsErrors() {
        RecipeParams params;
        params.targetWeight = -10.0;
        params.fillPressure = 15.0;
        params.pourFlow = 20.0;
        params.fillTimeout = -1.0;
        params.preinfuseFrameCount = 25;

        QStringList issues = params.validate();
        QVERIFY(issues.size() >= 5);
    }

    void validateSentinelPreinfuseFrameCount() {
        RecipeParams params;
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
        RecipeParams a, b;
        b.targetWeight = a.targetWeight + 10.0;
        QVERIFY(a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsEqualWhenOnlyDoseDiffers() {
        RecipeParams a, b;
        b.dose = a.dose + 5.0;
        QVERIFY(a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsEqualWhenOnlyVolumeDiffers() {
        RecipeParams a, b;
        b.targetVolume = a.targetVolume + 50.0;
        QVERIFY(a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenFlowDiffers() {
        RecipeParams a, b;
        b.pourFlow = a.pourFlow + 1.0;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenPressureDiffers() {
        RecipeParams a, b;
        b.infusePressure = a.infusePressure + 1.0;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenTimeDiffers() {
        RecipeParams a, b;
        b.holdTime = a.holdTime + 5.0;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenTempDiffers() {
        RecipeParams a, b;
        b.pourTemperature = a.pourTemperature + 2.0;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenEditorTypeDiffers() {
        RecipeParams a, b;
        a.editorType = EditorType::DFlow;
        b.editorType = EditorType::AFlow;
        QVERIFY(!a.frameAffectingFieldsEqual(b));
    }

    void frameFieldsNotEqualWhenBoolDiffers() {
        RecipeParams a, b;
        b.infuseEnabled = !a.infuseEnabled;
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

        RecipeParams params = RecipeParams::fromJson(json);
        QCOMPARE(params.pourPressure, 9.0);
        QCOMPARE(params.pourFlow, 3.5);  // flowLimit replaces pourFlow
    }

    void legacyPourStyleFlow() {
        QJsonObject json;
        json["pourStyle"] = "flow";
        json["pourPressure"] = 9.0;
        json["pourFlow"] = 2.0;
        json["pressureLimit"] = 6.0;

        RecipeParams params = RecipeParams::fromJson(json);
        QCOMPARE(params.pourFlow, 2.0);
        QCOMPARE(params.pourPressure, 6.0);  // pressureLimit replaces pourPressure
    }
};

QTEST_GUILESS_MAIN(tst_RecipeParams)
#include "tst_recipeparams.moc"
