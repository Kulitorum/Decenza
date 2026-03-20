#include "profileframe.h"
#include "../ble/protocol/de1characteristics.h"
#include <QRegularExpression>

// Convert a JSON value that may be string or number to double (de1app encodes numbers as strings)
static double jsonToDouble(const QJsonValue& val, double defaultVal = 0.0) {
    if (val.isString()) {
        bool ok;
        double d = val.toString().toDouble(&ok);
        if (!ok) {
            qWarning() << "jsonToDouble: failed to parse string" << val.toString() << "- using default" << defaultVal;
        }
        return ok ? d : defaultVal;
    }
    return val.toDouble(defaultVal);
}

QJsonObject ProfileFrame::toJson() const {
    QJsonObject obj;
    obj["name"] = name;
    obj["temperature"] = temperature;
    obj["sensor"] = sensor;
    obj["pump"] = pump;
    obj["transition"] = transition;
    obj["pressure"] = pressure;
    obj["flow"] = flow;
    obj["seconds"] = seconds;
    obj["volume"] = volume;

    // Exit condition (de1app nested format)
    // Note: weight-only exits (exitType == "weight") are NOT written to the exit object —
    // weight is app-side only, serialized separately as the standalone "weight" key below
    if (exitIf && !exitType.isEmpty()) {
        QJsonObject exitObj;
        if (exitType == "pressure_over") {
            exitObj["type"] = QStringLiteral("pressure");
            exitObj["condition"] = QStringLiteral("over");
            exitObj["value"] = exitPressureOver;
        } else if (exitType == "pressure_under") {
            exitObj["type"] = QStringLiteral("pressure");
            exitObj["condition"] = QStringLiteral("under");
            exitObj["value"] = exitPressureUnder;
        } else if (exitType == "flow_over") {
            exitObj["type"] = QStringLiteral("flow");
            exitObj["condition"] = QStringLiteral("over");
            exitObj["value"] = exitFlowOver;
        } else if (exitType == "flow_under") {
            exitObj["type"] = QStringLiteral("flow");
            exitObj["condition"] = QStringLiteral("under");
            exitObj["value"] = exitFlowUnder;
        } else if (exitType != "weight") {
            qWarning() << "ProfileFrame::toJson: unrecognized exitType" << exitType;
        }
        if (!exitObj.isEmpty()) obj["exit"] = exitObj;
    }

    // Weight exit (independent of exit object — app-side via scale)
    if (exitWeight > 0) obj["weight"] = exitWeight;

    // Limiter (de1app nested format)
    // Always save the limiter object for round-trip fidelity
    // (D-Flow profiles set range to 0.2 even when limiter value is 0)
    QJsonObject limiterObj;
    limiterObj["value"] = maxFlowOrPressure;
    limiterObj["range"] = maxFlowOrPressureRange;
    obj["limiter"] = limiterObj;

    // User notification popup
    if (!popup.isEmpty()) {
        obj["popup"] = popup;
    }

    return obj;
}

ProfileFrame ProfileFrame::fromJson(const QJsonObject& json) {
    ProfileFrame frame;
    frame.name = json["name"].toString();
    frame.temperature = jsonToDouble(json["temperature"], 93.0);
    frame.sensor = json["sensor"].toString("coffee");
    frame.pump = json["pump"].toString("pressure");
    frame.transition = json["transition"].toString("fast");
    frame.pressure = jsonToDouble(json["pressure"], 9.0);
    frame.flow = jsonToDouble(json["flow"], 2.0);
    frame.seconds = jsonToDouble(json["seconds"], 30.0);
    frame.volume = jsonToDouble(json["volume"], 0.0);

    // Exit conditions: try de1app nested object first, fall back to flat fields
    QJsonObject exitObj = json["exit"].toObject();
    if (!exitObj.isEmpty()) {
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
                qWarning() << "ProfileFrame::fromJson: unrecognized exit condition"
                           << exitCondition << "for type" << exitType << "- defaulting to over";
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
                qWarning() << "ProfileFrame::fromJson: unrecognized exit condition"
                           << exitCondition << "for type" << exitType << "- defaulting to over";
                frame.exitType = "flow_over";
                frame.exitFlowOver = exitValue;
            }
        } else if (exitType == "weight") {
            frame.exitType = "weight";
            frame.exitWeight = exitValue;
        } else {
            qWarning() << "ProfileFrame::fromJson: unrecognized exit type" << exitType << "- ignoring exit condition";
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
            qWarning() << "ProfileFrame::fromJson: unrecognized legacy exit_type"
                       << frame.exitType << "- disabling exit condition";
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
        frame.maxFlowOrPressure = jsonToDouble(limiterObj["value"], 0.0);
        frame.maxFlowOrPressureRange = jsonToDouble(limiterObj["range"], 0.6);
    } else {
        frame.maxFlowOrPressure = jsonToDouble(json["max_flow_or_pressure"], 0.0);
        frame.maxFlowOrPressureRange = jsonToDouble(json["max_flow_or_pressure_range"], 0.6);
    }

    frame.popup = json["popup"].toString();

    return frame;
}

ProfileFrame ProfileFrame::fromTclList(const QString& tclList) {
    // Parse de1app Tcl list format: {key value key value ...}
    // Example: {exit_if 1 flow 2.0 volume 100 transition fast exit_flow_under 0.0
    //           temperature 93.0 name {preinfusion} pressure 1.0 sensor coffee
    //           pump pressure exit_type pressure_over popup {$weight} seconds 10}

    ProfileFrame frame;
    QString cleaned = tclList.trimmed();

    // Remove outer braces if present
    if (cleaned.startsWith('{') && cleaned.endsWith('}')) {
        cleaned = cleaned.mid(1, cleaned.length() - 2);
    }

    // Parse key-value pairs
    // Handle braced values {content}, quoted strings "content", and simple words
    // Pattern: word + whitespace + ({braced} OR "quoted" OR simple_word)
    QRegularExpression re("(\\w+)\\s+(?:\\{([^}]*)\\}|\"([^\"]*)\"|([^\\s]+))");
    QRegularExpressionMatchIterator it = re.globalMatch(cleaned);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString key = match.captured(1);
        // Value is in capture group 2 (braced), 3 (quoted), or 4 (simple)
        QString value;
        if (!match.captured(2).isNull()) {
            value = match.captured(2);  // Braced value (may be empty string)
        } else if (!match.captured(3).isEmpty()) {
            value = match.captured(3);  // Quoted value
        } else {
            value = match.captured(4);  // Simple value
        }

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
    parts << QStringLiteral("exit_pressure_over") << QString::number(exitPressureOver, 'f', 2);
    parts << QStringLiteral("exit_pressure_under") << QString::number(exitPressureUnder, 'f', 2);
    parts << QStringLiteral("exit_flow_over") << QString::number(exitFlowOver, 'f', 2);
    parts << QStringLiteral("exit_flow_under") << QString::number(exitFlowUnder, 'f', 2);
    parts << QStringLiteral("max_flow_or_pressure") << QString::number(maxFlowOrPressure, 'f', 2);
    parts << QStringLiteral("max_flow_or_pressure_range") << QString::number(maxFlowOrPressureRange, 'f', 2);
    parts << QStringLiteral("weight") << QString::number(exitWeight, 'f', 1);
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
