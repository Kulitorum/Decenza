#include "profileframe.h"
#include "profilejson.h"
#include "profile.h"   // profileJsonToDouble
#include "../ble/protocol/de1characteristics.h"
#include <QRegularExpression>

// String-or-number parsing and the canonical precision table both live in
// profilejson.h — this file must not carry its own copy of either. It did, and a
// second identical jsonToDouble here is exactly how the two serializers drifted.
static double jsonToDouble(const QJsonValue& val, double defaultVal = 0.0) {
    return profileJsonToDouble(val, defaultVal);
}

QJsonObject ProfileFrame::toJson() const {
    // Canonical DE1 v2 serialization: numeric fields are string-encoded, matching
    // de1app / the tablet / Visualizer / reaprime. Decenza's own import stays
    // dual-tolerant via jsonToDouble(), so string output round-trips losslessly.
    // Precisions come from profilejson.h, and two of them deliberately DIFFER
    // from the historical Visualizer upload builder this replaced: `volume`
    // widened 0→1 dp so it agrees with toTclList(), and the limiter 1→2 dp to
    // match the editor's 0.01 step. So the step objects are equivalent, not
    // byte-identical — the earlier wording here claimed the latter.
    QJsonObject obj;
    obj["name"] = name;
    obj["temperature"] = ProfileJson::enc(temperature, ProfileJson::Temperature);
    obj["sensor"] = sensor;
    obj["pump"] = pump;
    obj["transition"] = transition;
    obj["pressure"] = ProfileJson::enc(pressure, ProfileJson::Pressure);
    obj["flow"] = ProfileJson::enc(flow, ProfileJson::Flow);
    obj["seconds"] = ProfileJson::enc(seconds, ProfileJson::Seconds);
    obj["volume"] = ProfileJson::enc(volume, ProfileJson::Volume);

    // Exit condition (de1app nested format)
    // Note: weight-only exits (exitType == "weight") are NOT written to the exit object —
    // weight is app-side only, serialized separately as the standalone "weight" key below
    if (exitIf && !exitType.isEmpty()) {
        QJsonObject exitObj;
        if (exitType == "pressure_over") {
            exitObj["type"] = QStringLiteral("pressure");
            exitObj["condition"] = QStringLiteral("over");
            exitObj["value"] = ProfileJson::enc(exitPressureOver, ProfileJson::Pressure);
        } else if (exitType == "pressure_under") {
            exitObj["type"] = QStringLiteral("pressure");
            exitObj["condition"] = QStringLiteral("under");
            exitObj["value"] = ProfileJson::enc(exitPressureUnder, ProfileJson::Pressure);
        } else if (exitType == "flow_over") {
            exitObj["type"] = QStringLiteral("flow");
            exitObj["condition"] = QStringLiteral("over");
            exitObj["value"] = ProfileJson::enc(exitFlowOver, ProfileJson::Flow);
        } else if (exitType == "flow_under") {
            exitObj["type"] = QStringLiteral("flow");
            exitObj["condition"] = QStringLiteral("under");
            exitObj["value"] = ProfileJson::enc(exitFlowUnder, ProfileJson::Flow);
        } else if (exitType != "weight") {
            qWarning() << "ProfileFrame::toJson: unrecognized exitType" << exitType;
        }
        if (!exitObj.isEmpty()) obj["exit"] = exitObj;
    }

    // Weight exit (independent of exit object — app-side via scale).
    // Omit when zero: reaprime reads an absent weight as "no weight exit"
    // (parseOptionalDouble → null), so omitting is the correct semantic.
    if (exitWeight > 0) obj["weight"] = ProfileJson::enc(exitWeight, ProfileJson::Weight);

    // Limiter (de1app nested format)
    // Always save the limiter object for round-trip fidelity
    // (D-Flow profiles set range to 0.2 even when limiter value is 0)
    QJsonObject limiterObj;
    limiterObj["value"] = ProfileJson::enc(maxFlowOrPressure, ProfileJson::Limiter);
    limiterObj["range"] = ProfileJson::enc(maxFlowOrPressureRange, ProfileJson::Limiter);
    obj["limiter"] = limiterObj;

    // User notification popup
    if (!popup.isEmpty()) {
        obj["popup"] = popup;
    }

    return obj;
}

const QSet<QString>& ProfileFrame::knownJsonKeys() {
    static const QSet<QString> k = {
        // The 13 canonical keys, as written by de1app, reaprime and ourselves.
        // `exit` and `limiter` are nested objects; their inner fields are read by
        // fromJson below and are not listed here.
        QStringLiteral("name"),        QStringLiteral("pump"),
        QStringLiteral("sensor"),      QStringLiteral("transition"),
        QStringLiteral("temperature"), QStringLiteral("pressure"),
        QStringLiteral("flow"),        QStringLiteral("seconds"),
        QStringLiteral("volume"),      QStringLiteral("weight"),
        QStringLiteral("exit"),        QStringLiteral("limiter"),
        QStringLiteral("popup"),

        // The flat de1app-style spellings of the same exit/limiter data. Our own
        // MCP surface and older files use these instead of the nested objects, so
        // they are equally "known" — same meaning, different encoding.
        QStringLiteral("exit_if"),             QStringLiteral("exit_type"),
        QStringLiteral("exit_pressure_over"),  QStringLiteral("exit_pressure_under"),
        QStringLiteral("exit_flow_over"),      QStringLiteral("exit_flow_under"),
        QStringLiteral("exit_weight"),
        QStringLiteral("max_flow_or_pressure"),
        QStringLiteral("max_flow_or_pressure_range"),
    };
    return k;
}

QStringList ProfileFrame::unknownJsonKeys(const QJsonObject& json) {
    QStringList unknown;
    const QSet<QString>& known = knownJsonKeys();
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        if (!known.contains(it.key()))
            unknown << it.key();
    }
    unknown.sort();
    return unknown;
}

namespace {

// Report a key nested inside `exit`/`limiter` that this build does not read.
//
// knownJsonKeys() lists `exit` and `limiter` as whole keys, so unknownJsonKeys()
// sees them as understood and never looks inside. That made their contents the one
// place a step could carry something unrecognised and pass silently — an
// `exit: {type, value, condition, tolerance}` from a future app version would drop
// `tolerance` with nothing said anywhere.
//
// Warns rather than invalidating, deliberately. Refusing would be the consistent
// choice with an unknown top-level step key, but nothing in any profile we have
// examined — 93 shipped, 93 golden, 96 reaprime, 371 exit objects — carries such a
// key, so a refusal path here would be written blind and would first execute on a
// user's machine. A warning turns the case from invisible into diagnosable in the
// field, which is what decides whether it is worth building the refusal at all.
//
// Phrased for whoever reads the log — often a user's own AI over MCP — so it names
// the consequence and the remedy rather than only the fact.
void warnUnmodelledNestedKeys(const QJsonObject& nested, const char* which,
                              const QSet<QString>& read)
{
    QStringList unmodelled;
    for (auto it = nested.constBegin(); it != nested.constEnd(); ++it) {
        if (!read.contains(it.key()))
            unmodelled << it.key();
    }
    if (unmodelled.isEmpty())
        return;
    unmodelled.sort();
    qWarning().noquote()
        << QStringLiteral("ProfileFrame::fromJson: the '%1' object in this step carries "
                          "setting(s) this build does not read: %2. They are being IGNORED, "
                          "so this frame may brew differently than the profile describes. "
                          "The profile was still loaded. Please report this with the profile "
                          "attached — it means another app writes a frame setting Decenza "
                          "does not implement yet.")
               .arg(QLatin1String(which), unmodelled.join(QStringLiteral(", ")));
}

// True for a value that is present but blank — de1app / Visualizer notation for
// "this frame does not use this setpoint". Distinct from absent (key missing)
// and from unparseable ("abc"), both of which must keep warning.
bool isBlankSetpoint(const QJsonValue& val) {
    return val.isString() && val.toString().trimmed().isEmpty();
}

}  // namespace

ProfileFrame ProfileFrame::fromJson(const QJsonObject& json) {
    ProfileFrame frame;
    frame.name = json["name"].toString();
    frame.temperature = jsonToDouble(json["temperature"], 93.0);
    frame.sensor = json["sensor"].toString("coffee");
    frame.pump = json["pump"].toString("pressure");
    frame.transition = json["transition"].toString("fast");
    // A frame drives EITHER pressure or flow, and de1app / the Visualizer write
    // the unused one as "" rather than omitting it. That blank is inapplicable,
    // not lost, so reading it must not warn — one reporter's log carried 128
    // "failed to parse string" lines from a single library browse, every one of
    // them a pressure frame's empty `flow` (#1658).
    //
    // Scoped to exactly that pair, and only when the pump says the field is
    // unused. Everything else — including a FLOW frame with an empty `flow`, or
    // an empty `seconds` — still goes through the warning path, because there a
    // blank means a real value was lost and the default silently fabricates one.
    // The resulting VALUE is identical either way — both arms yield the same
    // default the warning path would have returned. Only the log line differs.
    const bool flowDriven = frame.pump == QStringLiteral("flow");
    frame.pressure = (flowDriven && isBlankSetpoint(json["pressure"]))
                         ? 9.0 : jsonToDouble(json["pressure"], 9.0);
    frame.flow = (!flowDriven && isBlankSetpoint(json["flow"]))
                     ? 2.0 : jsonToDouble(json["flow"], 2.0);
    frame.seconds = jsonToDouble(json["seconds"], 30.0);
    frame.volume = jsonToDouble(json["volume"], 0.0);

    // Exit conditions: try de1app nested object first, fall back to flat fields
    QJsonObject exitObj = json["exit"].toObject();
    if (!exitObj.isEmpty()) {
        static const QSet<QString> exitRead = {QStringLiteral("type"),
                                               QStringLiteral("value"),
                                               QStringLiteral("condition")};
        warnUnmodelledNestedKeys(exitObj, "exit", exitRead);
        frame.exitIf = true;
        QString exitType = exitObj["type"].toString();
        double exitValue = jsonToDouble(exitObj["value"]);
        QString exitCondition = exitObj["condition"].toString("over");

        if (exitType == "pressure") {
            if (exitCondition == "over") {
                frame.exitType = "pressure_over";
                frame.exitPressureOver = exitValue;
            } else if (exitCondition == "under") {
                frame.exitType = "pressure_under";
                frame.exitPressureUnder = exitValue;
            } else {
                qWarning().noquote()
                    << QStringLiteral("ProfileFrame::fromJson: exit condition '%1' (type '%2') is "
                                      "not one this build understands, so it is being treated as "
                                      "'over'. If the profile meant 'under', this frame will exit "
                                      "on the WRONG side of its threshold — ending early or not at "
                                      "all. The profile was still loaded. Please report this.")
                           .arg(exitCondition, exitType);
                frame.exitType = "pressure_over";
                frame.exitPressureOver = exitValue;
            }
        } else if (exitType == "flow") {
            if (exitCondition == "over") {
                frame.exitType = "flow_over";
                frame.exitFlowOver = exitValue;
            } else if (exitCondition == "under") {
                frame.exitType = "flow_under";
                frame.exitFlowUnder = exitValue;
            } else {
                qWarning().noquote()
                    << QStringLiteral("ProfileFrame::fromJson: exit condition '%1' (type '%2') is "
                                      "not one this build understands, so it is being treated as "
                                      "'over'. If the profile meant 'under', this frame will exit "
                                      "on the WRONG side of its threshold — ending early or not at "
                                      "all. The profile was still loaded. Please report this.")
                           .arg(exitCondition, exitType);
                frame.exitType = "flow_over";
                frame.exitFlowOver = exitValue;
            }
        } else if (exitType == "weight") {
            frame.exitType = "weight";
            frame.exitWeight = exitValue;
        } else {
            qWarning().noquote()
                << QStringLiteral("ProfileFrame::fromJson: exit type '%1' is not one this build "
                                  "understands (expected pressure, flow or weight), so this "
                                  "frame's exit condition is being DROPPED — it will run its full "
                                  "duration instead of ending early. The profile was still loaded. "
                                  "Please report this with the profile attached.")
                       .arg(exitType);
            frame.exitIf = false;
        }
    } else {
        // Flat fields (legacy Decenza format, pre-migration)
        frame.exitIf = json["exit_if"].toBool(false);
        frame.exitType = json["exit_type"].toString();
        if (frame.exitIf && !frame.exitType.isEmpty()
            && frame.exitType != "pressure_over" && frame.exitType != "pressure_under"
            && frame.exitType != "flow_over" && frame.exitType != "flow_under"
            && frame.exitType != "weight") {
            qWarning().noquote()
                << QStringLiteral("ProfileFrame::fromJson: legacy exit_type '%1' is not one this "
                                  "build understands, so this frame's exit condition is being "
                                  "DROPPED — it will run its full duration instead of ending "
                                  "early. The profile was still loaded. Please report this with "
                                  "the profile attached.")
                       .arg(frame.exitType);
            frame.exitIf = false;
            frame.exitType.clear();
        }
        frame.exitPressureOver = jsonToDouble(json["exit_pressure_over"], 0.0);
        frame.exitPressureUnder = jsonToDouble(json["exit_pressure_under"], 0.0);
        frame.exitFlowOver = jsonToDouble(json["exit_flow_over"], 0.0);
        frame.exitFlowUnder = jsonToDouble(json["exit_flow_under"], 0.0);
    }

    // Weight exit: check de1app "weight" field first, then Decenza "exit_weight"
    // Weight exit is INDEPENDENT of exitIf — both can coexist on the same frame.
    // Never set exitIf/exitType here; weight is app-side only and must not override
    // the machine-side exit flag (e.g. exit_if=0 frames with weight exit would
    // otherwise round-trip as exitIf=true, causing perpetual "different" status).
    double weightExit = jsonToDouble(json["weight"], 0.0);
    if (weightExit <= 0) weightExit = jsonToDouble(json["exit_weight"], 0.0);
    if (weightExit > 0) {
        frame.exitWeight = weightExit;
    }

    // Limiter: try de1app nested object first, fall back to flat fields
    QJsonObject limiterObj = json["limiter"].toObject();
    if (!limiterObj.isEmpty()) {
        static const QSet<QString> limiterRead = {QStringLiteral("value"),
                                                  QStringLiteral("range")};
        warnUnmodelledNestedKeys(limiterObj, "limiter", limiterRead);
        frame.maxFlowOrPressure = jsonToDouble(limiterObj["value"], 0.0);
        frame.maxFlowOrPressureRange = jsonToDouble(limiterObj["range"], 0.6);
    } else {
        frame.maxFlowOrPressure = jsonToDouble(json["max_flow_or_pressure"], 0.0);
        frame.maxFlowOrPressureRange = jsonToDouble(json["max_flow_or_pressure_range"], 0.6);
    }

    frame.popup = json["popup"].toString();

    return frame;
}

// Tokenize a de1app Tcl frame into its key/value pairs.
//
// Shared by fromTclList() and unknownTclKeys() on purpose. A detector that
// tokenized differently from the parser would disagree about what counts as a
// key, and would miss precisely the keys the parser silently drops — the one
// thing it exists to catch. One tokenizer, so they cannot drift.
static QList<QPair<QString, QString>> tclKeyValues(const QString& tclList) {
    QString cleaned = tclList.trimmed();

    // Remove outer braces if present
    if (cleaned.startsWith('{') && cleaned.endsWith('}')) {
        cleaned = cleaned.mid(1, cleaned.length() - 2);
    }

    // Handle braced values {content}, quoted strings "content", and simple words
    // Pattern: word + whitespace + ({braced} OR "quoted" OR simple_word)
    static const QRegularExpression re(
        QStringLiteral("(\\w+)\\s+(?:\\{([^}]*)\\}|\"([^\"]*)\"|([^\\s]+))"));

    QList<QPair<QString, QString>> pairs;
    QRegularExpressionMatchIterator it = re.globalMatch(cleaned);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        // Value is in capture group 2 (braced), 3 (quoted), or 4 (simple)
        QString value;
        if (!match.captured(2).isNull()) {
            value = match.captured(2);  // Braced value (may be empty string)
        } else if (!match.captured(3).isEmpty()) {
            value = match.captured(3);  // Quoted value
        } else {
            value = match.captured(4);  // Simple value
        }
        pairs.append({match.captured(1), value});
    }
    return pairs;
}

const QSet<QString>& ProfileFrame::knownTclKeys() {
    // EXACTLY the keys fromTclList's if/else chain reads — no more.
    //
    // This deliberately does NOT reuse knownJsonKeys(). That set is a superset:
    // it carries `exit`, `limiter` (nested JSON objects with no Tcl spelling)
    // and `exit_weight` (the JSON spelling; Tcl says `weight`). Validating Tcl
    // against it meant a .tcl frame carrying `exit_weight 36` passed as fully
    // understood while fromTclList read nothing at all — the weight exit was
    // silently dropped and the shot ran past target weight.
    //
    // A key an allowlist blesses but no parser reads is worse than an unguarded
    // drop, because the guard certifies it as audited. Every entry here must
    // correspond to a branch in fromTclList; tst_builtinprofileformat asserts
    // that correspondence so the two cannot drift.
    static const QSet<QString> k = {
        QStringLiteral("name"),                QStringLiteral("temperature"),
        QStringLiteral("sensor"),              QStringLiteral("pump"),
        QStringLiteral("transition"),          QStringLiteral("pressure"),
        QStringLiteral("flow"),                QStringLiteral("seconds"),
        QStringLiteral("volume"),              QStringLiteral("weight"),
        QStringLiteral("popup"),               QStringLiteral("exit_if"),
        QStringLiteral("exit_type"),           QStringLiteral("exit_pressure_over"),
        QStringLiteral("exit_pressure_under"), QStringLiteral("exit_flow_over"),
        QStringLiteral("exit_flow_under"),     QStringLiteral("max_flow_or_pressure"),
        QStringLiteral("max_flow_or_pressure_range"),
    };
    return k;
}

QStringList ProfileFrame::malformedTclValues(const QString& tclList) {
    // The value-level twin of unknownTclKeys(). That one refuses a frame whose
    // KEY we do not understand; this refuses one whose known key carries a value
    // we cannot interpret.
    //
    // Without it the strict-import story has a hole precisely where it matters:
    // `value.toDouble()` returns 0.0 for anything unparseable, and 0 is a legal
    // value for every one of these fields, so nothing downstream can tell the
    // difference. `pressure ninebar` becomes a 0-bar frame and `seconds 10s`
    // becomes a 0-second one, both silently. The likeliest real source is a
    // locale decimal comma ("9,5") from a European-authored profile.
    static const QSet<QString> numericKeys = {
        QStringLiteral("temperature"),         QStringLiteral("pressure"),
        QStringLiteral("flow"),                QStringLiteral("seconds"),
        QStringLiteral("volume"),              QStringLiteral("weight"),
        QStringLiteral("exit_pressure_over"),  QStringLiteral("exit_pressure_under"),
        QStringLiteral("exit_flow_over"),      QStringLiteral("exit_flow_under"),
        QStringLiteral("max_flow_or_pressure"),
        QStringLiteral("max_flow_or_pressure_range"),
    };

    QStringList bad;
    for (const auto& kv : tclKeyValues(tclList)) {
        if (!numericKeys.contains(kv.first))
            continue;
        // An empty value is ABSENT, not malformed — de1app omits the axis a
        // frame's pump does not drive, and `ifexists` yields "" for it.
        if (kv.second.trimmed().isEmpty())
            continue;
        bool ok = false;
        kv.second.toDouble(&ok);
        if (!ok)
            bad << kv.first + QStringLiteral("=") + kv.second;
    }
    bad.sort();
    return bad;
}

QStringList ProfileFrame::unknownTclKeys(const QString& tclList) {
    QStringList unknown;
    const QSet<QString>& known = knownTclKeys();
    for (const auto& kv : tclKeyValues(tclList)) {
        if (!known.contains(kv.first) && !unknown.contains(kv.first))
            unknown << kv.first;
    }
    unknown.sort();
    return unknown;
}

ProfileFrame ProfileFrame::fromTclList(const QString& tclList) {
    // Parse de1app Tcl list format: {key value key value ...}
    // Example: {exit_if 1 flow 2.0 volume 100 transition fast exit_flow_under 0.0
    //           temperature 93.0 name {preinfusion} pressure 1.0 sensor coffee
    //           pump pressure exit_type pressure_over popup {$weight} seconds 10}

    ProfileFrame frame;

    // A de1app frame omits the axis its pump does not drive — a pressure frame
    // carries no `flow` key at all. ProfileFrame's member defaults exist for the
    // EDITOR (a new frame starts at 9 bar / 2 mL/s), and letting them stand in
    // for an absent key writes a value de1app never had into the profile.
    //
    // Measured on the corpus: 23 of the 89 stock .tcl files have at least one
    // frame that omits its inactive axis, and 19 of the 93 built-ins shipped
    // the exact member default there as a result. The DE1 ignores the inactive
    // axis so no shot changed, but every byte-level reader of our JSON saw an
    // invented number.
    frame.pressure = 0.0;
    frame.flow = 0.0;

    for (const auto& kv : tclKeyValues(tclList)) {
        const QString& key = kv.first;
        const QString& value = kv.second;

        if (key == "name") {
            frame.name = value;
        } else if (key == "temperature") {
            frame.temperature = value.toDouble();
        } else if (key == "sensor") {
            frame.sensor = value;
        } else if (key == "pump") {
            frame.pump = value;
        } else if (key == "transition") {
            frame.transition = (value == "smooth" || value == "slow") ? "smooth" : "fast";
        } else if (key == "pressure") {
            frame.pressure = value.toDouble();
        } else if (key == "flow") {
            frame.flow = value.toDouble();
        } else if (key == "seconds") {
            frame.seconds = value.toDouble();
        } else if (key == "volume") {
            frame.volume = value.toDouble();
        } else if (key == "exit_if") {
            frame.exitIf = (value == "1" || value == "true");
        } else if (key == "exit_type") {
            frame.exitType = value;
        } else if (key == "exit_pressure_over") {
            frame.exitPressureOver = value.toDouble();
        } else if (key == "exit_pressure_under") {
            frame.exitPressureUnder = value.toDouble();
        } else if (key == "exit_flow_over") {
            frame.exitFlowOver = value.toDouble();
        } else if (key == "exit_flow_under") {
            frame.exitFlowUnder = value.toDouble();
        } else if (key == "max_flow_or_pressure") {
            frame.maxFlowOrPressure = value.toDouble();
        } else if (key == "max_flow_or_pressure_range") {
            frame.maxFlowOrPressureRange = value.toDouble();
        } else if (key == "weight") {
            // Per-frame weight exit condition (requires scale)
            // NOTE: Weight exit is INDEPENDENT of exitIf - in de1app, a frame can have
            // exit_if 0 (no machine-side exit) with weight > 0 (app-side weight exit).
            // The weight check is always done app-side regardless of exit_if.
            double weightVal = value.toDouble();
            if (weightVal > 0) {
                frame.exitWeight = weightVal;
                // Do NOT set exitIf or exitType here - weight is independent
            }
        } else if (key == "popup") {
            // User notification message during this frame
            if (!value.isEmpty()) {
                frame.popup = value;
            }
        }
    }

    return frame;
}

QString ProfileFrame::toTclList() const {
    // Inverse of fromTclList() — produces de1app Tcl list format
    // Values with spaces go in braces; empty strings use {}
    auto tclVal = [](const QString& s) -> QString {
        if (s.isEmpty()) return QStringLiteral("{}");
        if (s.contains(' ') || s.contains('{') || s.contains('}'))
            return QStringLiteral("{%1}").arg(s);
        return s;
    };

    QStringList parts;
    parts << QStringLiteral("name") << tclVal(name);
    parts << QStringLiteral("temperature") << QString::number(temperature, 'f', 2);
    parts << QStringLiteral("sensor") << sensor;
    parts << QStringLiteral("pump") << pump;
    parts << QStringLiteral("transition") << transition;
    parts << QStringLiteral("pressure") << QString::number(pressure, 'f', 2);
    parts << QStringLiteral("flow") << QString::number(flow, 'f', 2);
    parts << QStringLiteral("seconds") << QString::number(seconds, 'f', 2);
    parts << QStringLiteral("volume") << QString::number(volume, 'f', 1);
    parts << QStringLiteral("exit_if") << (exitIf ? QStringLiteral("1") : QStringLiteral("0"));
    parts << QStringLiteral("exit_type") << tclVal(exitType);
    parts << QStringLiteral("exit_pressure_over") << ProfileJson::enc(exitPressureOver, ProfileJson::Pressure);
    parts << QStringLiteral("exit_pressure_under") << ProfileJson::enc(exitPressureUnder, ProfileJson::Pressure);
    parts << QStringLiteral("exit_flow_over") << ProfileJson::enc(exitFlowOver, ProfileJson::Flow);
    parts << QStringLiteral("exit_flow_under") << ProfileJson::enc(exitFlowUnder, ProfileJson::Flow);
    parts << QStringLiteral("max_flow_or_pressure") << ProfileJson::enc(maxFlowOrPressure, ProfileJson::Limiter);
    parts << QStringLiteral("max_flow_or_pressure_range") << ProfileJson::enc(maxFlowOrPressureRange, ProfileJson::Limiter);
    parts << QStringLiteral("weight") << ProfileJson::enc(exitWeight, ProfileJson::Weight);
    parts << QStringLiteral("popup") << tclVal(popup);

    return QStringLiteral("{%1}").arg(parts.join(' '));
}

ProfileFrame ProfileFrame::withSetpoint(double pressureOrFlow, double temp) const {
    ProfileFrame copy = *this;
    if (copy.pump == "flow") {
        copy.flow = pressureOrFlow;
    } else {
        copy.pressure = pressureOrFlow;
    }
    copy.temperature = temp;
    return copy;
}

uint8_t ProfileFrame::computeFlags() const {
    // IgnoreLimit controls the HEADER-level MinimumPressure/MaximumFlow limits,
    // NOT the per-frame extension frame limiters. De1app always sets this flag.
    // Extension frames (max_flow_or_pressure) work independently.
    uint8_t flags = DE1::FrameFlag::IgnoreLimit;

    // Flow vs pressure control
    if (pump == "flow") {
        flags |= DE1::FrameFlag::CtrlF;
    }

    // Mix temp vs basket temp
    if (sensor == "water") {
        flags |= DE1::FrameFlag::TMixTemp;
    }

    // Smooth transition (interpolate)
    if (transition == "smooth") {
        flags |= DE1::FrameFlag::Interpolate;
    }

    // Exit conditions
    if (exitIf) {
        if (exitType == "pressure_under") {
            flags |= DE1::FrameFlag::DoCompare;
            // DC_GT = 0 (less than), DC_CompF = 0 (pressure)
        } else if (exitType == "pressure_over") {
            flags |= DE1::FrameFlag::DoCompare | DE1::FrameFlag::DC_GT;
        } else if (exitType == "flow_under") {
            flags |= DE1::FrameFlag::DoCompare | DE1::FrameFlag::DC_CompF;
        } else if (exitType == "flow_over") {
            flags |= DE1::FrameFlag::DoCompare | DE1::FrameFlag::DC_GT | DE1::FrameFlag::DC_CompF;
        }
    }

    return flags;
}

double ProfileFrame::getSetVal() const {
    return (pump == "flow") ? flow : pressure;
}

double ProfileFrame::getTriggerVal() const {
    if (!exitIf) return 0.0;

    if (exitType == "pressure_under") return exitPressureUnder;
    if (exitType == "pressure_over") return exitPressureOver;
    if (exitType == "flow_under") return exitFlowUnder;
    if (exitType == "flow_over") return exitFlowOver;

    return 0.0;
}
