#include <QtTest>
#include <QJsonObject>

#include "profile/profileframe.h"
#include "ble/protocol/de1characteristics.h"

// Test ProfileFrame JSON/TCL serialization and BLE flag encoding.
// Expected flag values derived from de1app binary.tcl make_shot_flag / de1_packed_shot.
// ProfileFrame is a plain struct — no friend access needed.

class tst_ProfileFrame : public QObject {
    Q_OBJECT

private:
    // Helper: build a ProfileFrame with common defaults
    static ProfileFrame makeFrame(const QString& pump = "pressure",
                                  const QString& transition = "fast",
                                  const QString& sensor = "coffee") {
        ProfileFrame pf;
        pf.name = "test";
        pf.temperature = 93.0;
        pf.sensor = sensor;
        pf.pump = pump;
        pf.transition = transition;
        pf.pressure = 9.0;
        pf.flow = 2.0;
        pf.seconds = 30.0;
        pf.volume = 0.0;
        pf.exitIf = false;
        return pf;
    }

private slots:

    // ==========================================
    // JSON nested exit round-trip (de1app v2 format)
    // ==========================================

    void jsonNestedExitRoundTrip_data() {
        QTest::addColumn<QString>("exitType");
        QTest::addColumn<QString>("jsonType");
        QTest::addColumn<QString>("jsonCondition");
        QTest::addColumn<double>("exitValue");

        QTest::newRow("pressure_over")  << "pressure_over"  << "pressure" << "over"  << 4.0;
        QTest::newRow("pressure_under") << "pressure_under" << "pressure" << "under" << 2.0;
        QTest::newRow("flow_over")      << "flow_over"      << "flow"     << "over"  << 6.0;
        QTest::newRow("flow_under")     << "flow_under"     << "flow"     << "under" << 1.5;
    }

    void jsonNestedExitRoundTrip() {
        QFETCH(QString, exitType);
        QFETCH(QString, jsonType);
        QFETCH(QString, jsonCondition);
        QFETCH(double, exitValue);

        // Build frame with exit condition
        ProfileFrame original = makeFrame();
        original.exitIf = true;
        original.exitType = exitType;
        if (exitType == "pressure_over") original.exitPressureOver = exitValue;
        else if (exitType == "pressure_under") original.exitPressureUnder = exitValue;
        else if (exitType == "flow_over") original.exitFlowOver = exitValue;
        else if (exitType == "flow_under") original.exitFlowUnder = exitValue;

        // Serialize → nested exit object
        QJsonObject json = original.toJson();
        QVERIFY(json.contains("exit"));
        QJsonObject exitObj = json["exit"].toObject();
        QCOMPARE(exitObj["type"].toString(), jsonType);
        QCOMPARE(exitObj["condition"].toString(), jsonCondition);
        QCOMPARE(exitObj["value"].toDouble(), exitValue);

        // Deserialize back
        ProfileFrame parsed = ProfileFrame::fromJson(json);
        QVERIFY(parsed.exitIf);
        QCOMPARE(parsed.exitType, exitType);
    }

    void jsonWeightExitIndependentOfExitObj() {
        // de1app: weight is app-side only, independent of exit_if.
        // A frame can have exitIf=false + exitWeight > 0.
        ProfileFrame original = makeFrame();
        original.exitIf = false;
        original.exitWeight = 4.0;

        QJsonObject json = original.toJson();
        QVERIFY(!json.contains("exit"));       // No exit object (exitIf=false)
        QCOMPARE(json["weight"].toDouble(), 4.0);  // Weight is separate

        ProfileFrame parsed = ProfileFrame::fromJson(json);
        QVERIFY(!parsed.exitIf);
        QCOMPARE(parsed.exitWeight, 4.0);
    }

    void jsonWeightExitCoexistsWithMachineExit() {
        // Frame with BOTH machine-side exit AND app-side weight exit
        ProfileFrame original = makeFrame("flow");
        original.exitIf = true;
        original.exitType = "pressure_over";
        original.exitPressureOver = 4.0;
        original.exitWeight = 5.0;

        QJsonObject json = original.toJson();
        QVERIFY(json.contains("exit"));
        QCOMPARE(json["weight"].toDouble(), 5.0);

        ProfileFrame parsed = ProfileFrame::fromJson(json);
        QVERIFY(parsed.exitIf);
        QCOMPARE(parsed.exitType, QString("pressure_over"));
        QCOMPARE(parsed.exitPressureOver, 4.0);
        QCOMPARE(parsed.exitWeight, 5.0);
    }

    // ===== Legacy flat fields =====

    void jsonLegacyFlatExitFields() {
        // Old Decenza format: flat exit_if, exit_type, exit_pressure_over fields
        QJsonObject json;
        json["name"] = "legacy";
        json["temperature"] = 93.0;
        json["pump"] = "flow";
        json["flow"] = 4.0;
        json["seconds"] = 20.0;
        json["exit_if"] = true;
        json["exit_type"] = "pressure_over";
        json["exit_pressure_over"] = 4.0;
        json["exit_flow_over"] = 6.0;

        ProfileFrame pf = ProfileFrame::fromJson(json);
        QVERIFY(pf.exitIf);
        QCOMPARE(pf.exitType, QString("pressure_over"));
        QCOMPARE(pf.exitPressureOver, 4.0);
        QCOMPARE(pf.exitFlowOver, 6.0);
    }

    void jsonLegacyFlatLimiter() {
        // Old Decenza format: flat max_flow_or_pressure
        QJsonObject json;
        json["name"] = "legacy limiter";
        json["temperature"] = 93.0;
        json["pump"] = "pressure";
        json["pressure"] = 9.0;
        json["seconds"] = 30.0;
        json["max_flow_or_pressure"] = 6.0;
        json["max_flow_or_pressure_range"] = 1.0;

        ProfileFrame pf = ProfileFrame::fromJson(json);
        QCOMPARE(pf.maxFlowOrPressure, 6.0);
        QCOMPARE(pf.maxFlowOrPressureRange, 1.0);
    }

    // ===== Limiter nested round-trip =====

    void jsonLimiterAlwaysSaved() {
        // D-Flow pattern: limiter value=0, range=0.2 always serialized for fidelity
        ProfileFrame pf = makeFrame("flow");
        pf.maxFlowOrPressure = 0.0;
        pf.maxFlowOrPressureRange = 0.2;

        QJsonObject json = pf.toJson();
        QVERIFY(json.contains("limiter"));
        QJsonObject lim = json["limiter"].toObject();
        QCOMPARE(lim["value"].toDouble(), 0.0);
        QCOMPARE(lim["range"].toDouble(), 0.2);
    }

    void jsonLimiterWithValue() {
        ProfileFrame pf = makeFrame("pressure");
        pf.maxFlowOrPressure = 6.0;
        pf.maxFlowOrPressureRange = 1.0;

        QJsonObject json = pf.toJson();
        ProfileFrame parsed = ProfileFrame::fromJson(json);
        QCOMPARE(parsed.maxFlowOrPressure, 6.0);
        QCOMPARE(parsed.maxFlowOrPressureRange, 1.0);
    }

    // ===== Full round-trip =====

    void jsonAllFieldsRoundTrip() {
        ProfileFrame original;
        original.name = "full test";
        original.temperature = 88.5;
        original.sensor = "water";
        original.pump = "flow";
        original.transition = "smooth";
        original.pressure = 3.0;
        original.flow = 4.5;
        original.seconds = 15.0;
        original.volume = 100.0;
        original.exitIf = true;
        original.exitType = "flow_under";
        original.exitFlowUnder = 1.5;
        original.exitWeight = 5.0;
        original.maxFlowOrPressure = 9.0;
        original.maxFlowOrPressureRange = 0.8;
        original.popup = "$weight";

        QJsonObject json = original.toJson();
        ProfileFrame parsed = ProfileFrame::fromJson(json);

        QCOMPARE(parsed.name, original.name);
        QCOMPARE(parsed.temperature, original.temperature);
        QCOMPARE(parsed.sensor, original.sensor);
        QCOMPARE(parsed.pump, original.pump);
        QCOMPARE(parsed.transition, original.transition);
        QCOMPARE(parsed.pressure, original.pressure);
        QCOMPARE(parsed.flow, original.flow);
        QCOMPARE(parsed.seconds, original.seconds);
        QCOMPARE(parsed.volume, original.volume);
        QCOMPARE(parsed.exitIf, original.exitIf);
        QCOMPARE(parsed.exitType, original.exitType);
        QCOMPARE(parsed.exitFlowUnder, original.exitFlowUnder);
        QCOMPARE(parsed.exitWeight, original.exitWeight);
        QCOMPARE(parsed.maxFlowOrPressure, original.maxFlowOrPressure);
        QCOMPARE(parsed.maxFlowOrPressureRange, original.maxFlowOrPressureRange);
        QCOMPARE(parsed.popup, original.popup);
    }

    // ==========================================
    // TCL Parse (de1app frame format)
    // ==========================================

    void tclStandardFrame() {
        QString tcl = "{name {preinfusion} temperature 88.0 sensor coffee "
                      "pump flow transition fast pressure 1.0 flow 4.0 "
                      "seconds 20.0 volume 0.0 exit_if 1 exit_type pressure_over "
                      "exit_pressure_over 4.0 exit_pressure_under 0.0 "
                      "exit_flow_over 6.0 exit_flow_under 0.0 "
                      "max_flow_or_pressure 0.0 max_flow_or_pressure_range 0.6 "
                      "weight 0.0 popup {}}";

        ProfileFrame pf = ProfileFrame::fromTclList(tcl);
        QCOMPARE(pf.name, QString("preinfusion"));
        QCOMPARE(pf.temperature, 88.0);
        QCOMPARE(pf.pump, QString("flow"));
        QVERIFY(pf.exitIf);
        QCOMPARE(pf.exitType, QString("pressure_over"));
        QCOMPARE(pf.exitPressureOver, 4.0);
        QCOMPARE(pf.exitFlowOver, 6.0);
        QCOMPARE(pf.maxFlowOrPressure, 0.0);
        QCOMPARE(pf.maxFlowOrPressureRange, 0.6);
    }

    void tclTransitionSlowMapsToSmooth() {
        // de1app uses "slow", Decenza normalizes to "smooth"
        QString tcl = "{name decline temperature 93.0 pump pressure "
                      "pressure 4.0 seconds 25.0 transition slow sensor coffee "
                      "exit_if 0}";
        ProfileFrame pf = ProfileFrame::fromTclList(tcl);
        QCOMPARE(pf.transition, QString("smooth"));
    }

    void tclTransitionFastStaysFast() {
        QString tcl = "{name hold temperature 93.0 pump pressure "
                      "pressure 9.0 seconds 10.0 transition fast sensor coffee "
                      "exit_if 0}";
        ProfileFrame pf = ProfileFrame::fromTclList(tcl);
        QCOMPARE(pf.transition, QString("fast"));
    }

    void tclBracedNameWithSpaces() {
        QString tcl = "{name {rise and hold} temperature 93.0 pump pressure "
                      "pressure 9.0 seconds 30.0 exit_if 0 transition fast sensor coffee}";
        ProfileFrame pf = ProfileFrame::fromTclList(tcl);
        QCOMPARE(pf.name, QString("rise and hold"));
    }

    void tclWeightIndependentOfExitIf() {
        // de1app: exit_if 0 with weight 4.0 → weight exit set, exitIf stays false
        QString tcl = "{name infuse temperature 93.0 pump pressure "
                      "pressure 3.0 seconds 20.0 volume 100.0 "
                      "exit_if 0 weight 4.0 transition fast sensor coffee}";
        ProfileFrame pf = ProfileFrame::fromTclList(tcl);
        QVERIFY(!pf.exitIf);
        QCOMPARE(pf.exitWeight, 4.0);
    }

    void tclZeroWeightNotStored() {
        // weight 0.0 should NOT set exitWeight
        QString tcl = "{name pour temperature 93.0 pump flow flow 2.0 "
                      "seconds 30.0 exit_if 0 weight 0.0 transition fast sensor coffee}";
        ProfileFrame pf = ProfileFrame::fromTclList(tcl);
        QCOMPARE(pf.exitWeight, 0.0);
    }

    void tclRoundTrip() {
        ProfileFrame original;
        original.name = "rise and hold";
        original.temperature = 93.0;
        original.sensor = "coffee";
        original.pump = "pressure";
        original.transition = "smooth";
        original.pressure = 9.0;
        original.flow = 2.0;
        original.seconds = 30.0;
        original.volume = 0.0;
        original.exitIf = true;
        original.exitType = "pressure_over";
        original.exitPressureOver = 4.0;
        original.maxFlowOrPressure = 6.0;
        original.maxFlowOrPressureRange = 1.0;
        original.exitWeight = 3.5;

        QString tcl = original.toTclList();
        ProfileFrame parsed = ProfileFrame::fromTclList(tcl);

        QCOMPARE(parsed.name, original.name);
        QCOMPARE(parsed.temperature, original.temperature);
        QCOMPARE(parsed.pump, original.pump);
        QCOMPARE(parsed.transition, original.transition);
        QCOMPARE(parsed.pressure, original.pressure);
        QVERIFY(parsed.exitIf);
        QCOMPARE(parsed.exitType, original.exitType);
        QCOMPARE(parsed.exitPressureOver, original.exitPressureOver);
        QCOMPARE(parsed.maxFlowOrPressure, original.maxFlowOrPressure);
        QCOMPARE(parsed.exitWeight, original.exitWeight);
    }

    // ==========================================
    // computeFlags (de1app binary.tcl make_shot_flag / de1_packed_shot)
    //
    // de1app flag bits:
    //   CtrlF=0x01, DoCompare=0x02, DC_GT=0x04, DC_CompF=0x08,
    //   TMixTemp=0x10, Interpolate=0x20, IgnoreLimit=0x40
    //
    // de1app always starts with {IgnoreLimit} then adds flags based on
    // pump/sensor/transition/exit_if+exit_type.
    // ==========================================

    void computeFlagsExhaustive_data() {
        QTest::addColumn<QString>("pump");
        QTest::addColumn<QString>("transition");
        QTest::addColumn<QString>("sensor");
        QTest::addColumn<bool>("exitIf");
        QTest::addColumn<QString>("exitType");
        QTest::addColumn<uint8_t>("expectedFlags");

        // Base: IgnoreLimit (0x40) always set
        // de1app binary.tcl line 914: set features {IgnoreLimit}

        // No exit conditions
        QTest::newRow("pressure/fast/coffee/no-exit")
            << "pressure" << "fast" << "coffee" << false << "" << uint8_t(0x40);
        QTest::newRow("flow/fast/coffee/no-exit")
            << "flow" << "fast" << "coffee" << false << "" << uint8_t(0x41);
        QTest::newRow("pressure/smooth/coffee/no-exit")
            << "pressure" << "smooth" << "coffee" << false << "" << uint8_t(0x60);
        QTest::newRow("flow/smooth/coffee/no-exit")
            << "flow" << "smooth" << "coffee" << false << "" << uint8_t(0x61);
        QTest::newRow("pressure/fast/water/no-exit")
            << "pressure" << "fast" << "water" << false << "" << uint8_t(0x50);
        QTest::newRow("flow/fast/water/no-exit")
            << "flow" << "fast" << "water" << false << "" << uint8_t(0x51);

        // All exit types with pressure pump
        // de1app: pressure_under → DoCompare (DC_GT=0, DC_CompF=0)
        QTest::newRow("pressure/exit-pressure-under")
            << "pressure" << "fast" << "coffee" << true << "pressure_under" << uint8_t(0x42);
        // de1app: pressure_over → DoCompare | DC_GT
        QTest::newRow("pressure/exit-pressure-over")
            << "pressure" << "fast" << "coffee" << true << "pressure_over" << uint8_t(0x46);
        // de1app: flow_under → DoCompare | DC_CompF
        QTest::newRow("pressure/exit-flow-under")
            << "pressure" << "fast" << "coffee" << true << "flow_under" << uint8_t(0x4A);
        // de1app: flow_over → DoCompare | DC_GT | DC_CompF
        QTest::newRow("pressure/exit-flow-over")
            << "pressure" << "fast" << "coffee" << true << "flow_over" << uint8_t(0x4E);

        // All exit types with flow pump (add CtrlF=0x01)
        QTest::newRow("flow/exit-pressure-under")
            << "flow" << "fast" << "coffee" << true << "pressure_under" << uint8_t(0x43);
        QTest::newRow("flow/exit-pressure-over")
            << "flow" << "fast" << "coffee" << true << "pressure_over" << uint8_t(0x47);
        QTest::newRow("flow/exit-flow-under")
            << "flow" << "fast" << "coffee" << true << "flow_under" << uint8_t(0x4B);
        QTest::newRow("flow/exit-flow-over")
            << "flow" << "fast" << "coffee" << true << "flow_over" << uint8_t(0x4F);

        // Combined: flow + smooth + water + exit
        QTest::newRow("flow/smooth/water/exit-pressure-over")
            << "flow" << "smooth" << "water" << true << "pressure_over" << uint8_t(0x77);
        // 0x01(CtrlF) | 0x02(DoCompare) | 0x04(DC_GT) | 0x10(TMixTemp) | 0x20(Interpolate) | 0x40(IgnoreLimit)
    }

    void computeFlagsExhaustive() {
        QFETCH(QString, pump);
        QFETCH(QString, transition);
        QFETCH(QString, sensor);
        QFETCH(bool, exitIf);
        QFETCH(QString, exitType);
        QFETCH(uint8_t, expectedFlags);

        ProfileFrame pf = makeFrame(pump, transition, sensor);
        pf.exitIf = exitIf;
        pf.exitType = exitType;
        if (exitType == "pressure_over") pf.exitPressureOver = 4.0;
        else if (exitType == "pressure_under") pf.exitPressureUnder = 2.0;
        else if (exitType == "flow_over") pf.exitFlowOver = 6.0;
        else if (exitType == "flow_under") pf.exitFlowUnder = 1.5;

        QCOMPARE(pf.computeFlags(), expectedFlags);
    }

    // ==========================================
    // getSetVal (de1app binary.tcl line 917-921)
    // ==========================================

    void getSetVal_data() {
        QTest::addColumn<QString>("pump");
        QTest::addColumn<double>("pressure");
        QTest::addColumn<double>("flow");
        QTest::addColumn<double>("expected");

        // de1app: if pump=="flow" → SetVal = flow, else SetVal = pressure
        QTest::newRow("pressure pump") << "pressure" << 9.0 << 2.0 << 9.0;
        QTest::newRow("flow pump")     << "flow"     << 9.0 << 2.5 << 2.5;
    }

    void getSetVal() {
        QFETCH(QString, pump);
        QFETCH(double, pressure);
        QFETCH(double, flow);
        QFETCH(double, expected);

        ProfileFrame pf;
        pf.pump = pump;
        pf.pressure = pressure;
        pf.flow = flow;
        QCOMPARE(pf.getSetVal(), expected);
    }

    // ==========================================
    // getTriggerVal (de1app binary.tcl line 934-958)
    // ==========================================

    void getTriggerVal_data() {
        QTest::addColumn<bool>("exitIf");
        QTest::addColumn<QString>("exitType");
        QTest::addColumn<double>("expected");

        // de1app: if exit_if==0 → TriggerVal = 0
        QTest::newRow("no exit")          << false << "pressure_over" << 0.0;
        // de1app: each exit_type maps to its corresponding value
        QTest::newRow("pressure_under")   << true  << "pressure_under" << 2.0;
        QTest::newRow("pressure_over")    << true  << "pressure_over"  << 4.0;
        QTest::newRow("flow_under")       << true  << "flow_under"     << 1.5;
        QTest::newRow("flow_over")        << true  << "flow_over"      << 6.0;
    }

    void getTriggerVal() {
        QFETCH(bool, exitIf);
        QFETCH(QString, exitType);
        QFETCH(double, expected);

        ProfileFrame pf;
        pf.exitIf = exitIf;
        pf.exitType = exitType;
        pf.exitPressureUnder = 2.0;
        pf.exitPressureOver = 4.0;
        pf.exitFlowUnder = 1.5;
        pf.exitFlowOver = 6.0;
        QCOMPARE(pf.getTriggerVal(), expected);
    }

    // ==========================================
    // withSetpoint immutability
    // ==========================================

    void withSetpointFlowPump() {
        ProfileFrame original = makeFrame("flow");
        original.flow = 2.0;
        original.temperature = 93.0;

        ProfileFrame copy = original.withSetpoint(3.5, 88.0);
        QCOMPARE(copy.flow, 3.5);
        QCOMPARE(copy.temperature, 88.0);
        // Original unchanged
        QCOMPARE(original.flow, 2.0);
        QCOMPARE(original.temperature, 93.0);
    }

    void withSetpointPressurePump() {
        ProfileFrame original = makeFrame("pressure");
        original.pressure = 9.0;
        original.temperature = 93.0;

        ProfileFrame copy = original.withSetpoint(6.0, 88.0);
        QCOMPARE(copy.pressure, 6.0);
        QCOMPARE(copy.temperature, 88.0);
        // Original unchanged
        QCOMPARE(original.pressure, 9.0);
        QCOMPARE(original.temperature, 93.0);
    }

    // ==========================================
    // isFlowControl / needsExtensionFrame
    // ==========================================

    void isFlowControl() {
        ProfileFrame flow = makeFrame("flow");
        QVERIFY(flow.isFlowControl());

        ProfileFrame pressure = makeFrame("pressure");
        QVERIFY(!pressure.isFlowControl());
    }

    void needsExtensionFrame() {
        // de1app binary.tcl line 971: extension frame when max_flow_or_pressure != 0
        ProfileFrame withLimiter = makeFrame();
        withLimiter.maxFlowOrPressure = 6.0;
        QVERIFY(withLimiter.needsExtensionFrame());

        ProfileFrame noLimiter = makeFrame();
        noLimiter.maxFlowOrPressure = 0.0;
        QVERIFY(!noLimiter.needsExtensionFrame());
    }

    // ==========================================
    // Popup field round-trip
    // ==========================================

    void popupRoundTrip() {
        ProfileFrame pf = makeFrame();
        pf.popup = "$weight";

        QJsonObject json = pf.toJson();
        QCOMPARE(json["popup"].toString(), QString("$weight"));

        ProfileFrame parsed = ProfileFrame::fromJson(json);
        QCOMPARE(parsed.popup, QString("$weight"));
    }

    void popupEmptyNotSerialized() {
        ProfileFrame pf = makeFrame();
        pf.popup = "";

        QJsonObject json = pf.toJson();
        QVERIFY(!json.contains("popup"));
    }

    void popupTclBracedRoundTrip() {
        ProfileFrame pf = makeFrame();
        pf.popup = "$weight";

        QString tcl = pf.toTclList();
        ProfileFrame parsed = ProfileFrame::fromTclList(tcl);
        QCOMPARE(parsed.popup, QString("$weight"));
    }
};

QTEST_GUILESS_MAIN(tst_ProfileFrame)
#include "tst_profileframe.moc"
