#include "profile/de1apptclfields.h"

#include "profile/profile.h"       // profileJsonToDouble / profileJsonToBool
#include "profile/profilejson.h"   // ProfileJson::enc — the writer's precision policy

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

namespace De1AppTcl {

bool isAdvancedType(const QString& profileType)
{
    return profileType.startsWith(QLatin1String("settings_2c"));
}

const QVector<ScalarField>& scalarFields()
{
    // clang-format off
    static const QVector<ScalarField> kFields = {
        // --- The four dual-spelled fields. Which spelling wins depends on the
        // profile type; everything else below reads the same key either way. ---
        { QStringLiteral("target_weight"),
          QStringLiteral("final_desired_shot_weight"),
          QStringLiteral("final_desired_shot_weight_advanced"),
          Kind::Number, ProfileJson::TargetMass },
        { QStringLiteral("target_volume"),
          QStringLiteral("final_desired_shot_volume"),
          QStringLiteral("final_desired_shot_volume_advanced"),
          Kind::Number, ProfileJson::TargetMass },
        { QStringLiteral("maximum_pressure_range_advanced"),
          QStringLiteral("maximum_pressure_range_default"),
          QStringLiteral("maximum_pressure_range_advanced"),
          Kind::Number, ProfileJson::Limiter },
        { QStringLiteral("maximum_flow_range_advanced"),
          QStringLiteral("maximum_flow_range_default"),
          QStringLiteral("maximum_flow_range_advanced"),
          Kind::Number, ProfileJson::Limiter },

        // --- Machine limits. A profile that omits these gets NO limiter in
        // de1app, where Profile's own defaults are 12 bar / 6 mL/s. 28 of 89
        // de1app profiles omit them, so taking the member default would switch
        // on a limiter de1app never applies.
        //
        // de1plus/profile.tcl:513-519, in convert_all_legacy_to_v2 — which is
        // this exact operation, `profiles/*.tcl` → `profiles_v2/*.json`. It
        // presets these ("Disable limits by default") and THEN overlays the file
        // with `array set`, so a key the file omits keeps the preset. ---
        { QStringLiteral("maximum_pressure"), QStringLiteral("maximum_pressure"),
          QStringLiteral("maximum_pressure"), Kind::Number, ProfileJson::Pressure, -1, 0.0 },
        { QStringLiteral("maximum_flow"), QStringLiteral("maximum_flow"),
          QStringLiteral("maximum_flow"), Kind::Number, ProfileJson::Flow, -1, 0.0 },
        { QStringLiteral("maximum_pressure_range_default"), QStringLiteral("maximum_pressure_range_default"),
          QStringLiteral("maximum_pressure_range_default"), Kind::Number, ProfileJson::Limiter },
        { QStringLiteral("maximum_flow_range_default"), QStringLiteral("maximum_flow_range_default"),
          QStringLiteral("maximum_flow_range_default"), Kind::Number, ProfileJson::Limiter },
        // minimum_pressure is Decenza's canonical spelling of de1app's
        // flow_profile_minimum_pressure; the alias below is the same value kept
        // under de1app's own name so its editor still reads it.
        { QStringLiteral("minimum_pressure"), QStringLiteral("flow_profile_minimum_pressure"),
          QStringLiteral("flow_profile_minimum_pressure"), Kind::Number, ProfileJson::Pressure },
        { QStringLiteral("flow_profile_minimum_pressure"), QStringLiteral("flow_profile_minimum_pressure"),
          QStringLiteral("flow_profile_minimum_pressure"), Kind::Number, ProfileJson::Pressure },

        // --- Temperature ---
        { QStringLiteral("espresso_temperature"), QStringLiteral("espresso_temperature"),
          QStringLiteral("espresso_temperature"), Kind::Number, ProfileJson::Temperature },
        { QStringLiteral("tank_desired_water_temperature"), QStringLiteral("tank_desired_water_temperature"),
          QStringLiteral("tank_desired_water_temperature"), Kind::Number, ProfileJson::TankTemp },
        { QStringLiteral("temp_steps_enabled"), QStringLiteral("espresso_temperature_steps_enabled"),
          QStringLiteral("espresso_temperature_steps_enabled"), Kind::Boolean },
        { QStringLiteral("temperature_presets"), QStringLiteral("espresso_temperature_0"),
          QStringLiteral("espresso_temperature_0"), Kind::Number, ProfileJson::Temperature, 0 },
        { QStringLiteral("temperature_presets"), QStringLiteral("espresso_temperature_1"),
          QStringLiteral("espresso_temperature_1"), Kind::Number, ProfileJson::Temperature, 1 },
        { QStringLiteral("temperature_presets"), QStringLiteral("espresso_temperature_2"),
          QStringLiteral("espresso_temperature_2"), Kind::Number, ProfileJson::Temperature, 2 },
        { QStringLiteral("temperature_presets"), QStringLiteral("espresso_temperature_3"),
          QStringLiteral("espresso_temperature_3"), Kind::Number, ProfileJson::Temperature, 3 },

        // --- de1app's pressure editor (settings_2a) ---
        { QStringLiteral("preinfusion_time"), QStringLiteral("preinfusion_time"),
          QStringLiteral("preinfusion_time"), Kind::Number, ProfileJson::Seconds },
        { QStringLiteral("preinfusion_flow_rate"), QStringLiteral("preinfusion_flow_rate"),
          QStringLiteral("preinfusion_flow_rate"), Kind::Number, ProfileJson::Flow },
        { QStringLiteral("preinfusion_stop_pressure"), QStringLiteral("preinfusion_stop_pressure"),
          QStringLiteral("preinfusion_stop_pressure"), Kind::Number, ProfileJson::Pressure },
        { QStringLiteral("espresso_pressure"), QStringLiteral("espresso_pressure"),
          QStringLiteral("espresso_pressure"), Kind::Number, ProfileJson::Pressure },
        { QStringLiteral("espresso_hold_time"), QStringLiteral("espresso_hold_time"),
          QStringLiteral("espresso_hold_time"), Kind::Number, ProfileJson::Seconds },
        { QStringLiteral("espresso_decline_time"), QStringLiteral("espresso_decline_time"),
          QStringLiteral("espresso_decline_time"), Kind::Number, ProfileJson::Seconds },
        { QStringLiteral("pressure_end"), QStringLiteral("pressure_end"),
          QStringLiteral("pressure_end"), Kind::Number, ProfileJson::Pressure },

        // --- de1app's flow editor (settings_2b). flow_profile_preinfusion and
        // _preinfusion_time LOOK like aliases of preinfusion_flow_rate /
        // preinfusion_time and are not — they are the flow editor's own values,
        // and they differ from the pressure editor's in 61 of 62 built-ins. ---
        { QStringLiteral("flow_profile_hold"), QStringLiteral("flow_profile_hold"),
          QStringLiteral("flow_profile_hold"), Kind::Number, ProfileJson::Flow },
        { QStringLiteral("flow_profile_hold_time"), QStringLiteral("flow_profile_hold_time"),
          QStringLiteral("flow_profile_hold_time"), Kind::Number, ProfileJson::Seconds },
        { QStringLiteral("flow_profile_decline"), QStringLiteral("flow_profile_decline"),
          QStringLiteral("flow_profile_decline"), Kind::Number, ProfileJson::Flow },
        { QStringLiteral("flow_profile_decline_time"), QStringLiteral("flow_profile_decline_time"),
          QStringLiteral("flow_profile_decline_time"), Kind::Number, ProfileJson::Seconds },
        { QStringLiteral("flow_profile_preinfusion"), QStringLiteral("flow_profile_preinfusion"),
          QStringLiteral("flow_profile_preinfusion"), Kind::Number, ProfileJson::Flow },
        { QStringLiteral("flow_profile_preinfusion_time"), QStringLiteral("flow_profile_preinfusion_time"),
          QStringLiteral("flow_profile_preinfusion_time"), Kind::Number, ProfileJson::Seconds },

        // --- Metadata other DE1 apps act on ---
        { QStringLiteral("hidden"), QStringLiteral("profile_hide"),
          QStringLiteral("profile_hide"), Kind::Boolean },
    };
    // clang-format on
    return kFields;
}

QString tclKeyFor(const QString& canonical, const QString& profileType)
{
    const bool advanced = isAdvancedType(profileType);
    for (const ScalarField& f : scalarFields()) {
        if (f.canonical != canonical || f.presetIndex >= 0)
            continue;
        return advanced ? f.tclAdvanced : f.tclSimple;
    }
    return QString();
}

QString extractValue(const QString& content, const QString& varName)
{
    // Use \b so e.g. "preinfusion_time" does not match the tail of
    // "flow_profile_preinfusion_time" (substring false-positive).
    const QString pattern = QLatin1String("\\b") + varName;

    QRegularExpression reBraced(pattern + QLatin1String("\\s+\\{([^}]*)\\}"));
    QRegularExpressionMatch match = reBraced.match(content);
    if (match.hasMatch())
        return match.captured(1);

    QRegularExpression reQuoted(pattern + QLatin1String("\\s+\"([^\"]*)\""));
    match = reQuoted.match(content);
    if (match.hasMatch())
        return match.captured(1);

    QRegularExpression reSimple(pattern + QLatin1String("\\s+(\\S+)"));
    match = reSimple.match(content);
    return match.hasMatch() ? match.captured(1) : QString();
}

ScalarRead readScalar(const QString& content, const QString& canonical,
                      const QString& profileType, double fallback)
{
    const bool advanced = isAdvancedType(profileType);
    for (const ScalarField& f : scalarFields()) {
        if (f.canonical != canonical || f.presetIndex >= 0)
            continue;

        const QString tclKey = advanced ? f.tclAdvanced : f.tclSimple;
        const QString raw = extractValue(content, tclKey);
        if (raw.isEmpty())
            return {f.whenAbsent.value_or(fallback), ReadStatus::Absent, raw, tclKey};

        bool ok = false;
        const double v = raw.toDouble(&ok);
        if (!ok)
            return {fallback, ReadStatus::Malformed, raw, tclKey};
        return {v, ReadStatus::Parsed, raw, tclKey};
    }

    // No table entry for this canonical name. Every caller passes a string
    // literal, so this is a typo in the reader, not bad input — and it would
    // otherwise be a silent no-op leaving the member at its constructor
    // default, which is the exact signature of the drift this file exists to
    // prevent (espresso_pressure read 9.2 on 23 profiles because 9.2 is the
    // default, not because any file said so).
    Q_ASSERT_X(false, "De1AppTcl::readScalar", qPrintable(canonical));
    qWarning() << "De1AppTcl::readScalar: no table entry for canonical key" << canonical
               << "— value left at the caller's default" << fallback;
    return {fallback, ReadStatus::Absent, QString(), QString()};
}

const QStringList& nonScalarTclKeys()
{
    static const QStringList kKeys = {
        // Handled by the reader as first-class profile properties, not scalars.
        QStringLiteral("profile_title"),
        QStringLiteral("author"),
        QStringLiteral("profile_notes"),
        QStringLiteral("settings_profile_type"),
        QStringLiteral("beverage_type"),
        QStringLiteral("read_only"),
        QStringLiteral("advanced_shot"),
        // Language: preserved verbatim as the JSON `lang` key. Not a scalar.
        QStringLiteral("profile_language"),
        // NumberOfPreinfuseFrames. de1app stores a literal here but DERIVES the
        // value during frame generation for simple profiles, so comparing the
        // literal against our derived count reports drift that is not there.
        // The frame comparison already covers what the DE1 actually receives.
        QStringLiteral("final_desired_shot_volume_advanced_count_start"),
    };
    return kKeys;
}

QStringList assignedTclKeys(const QString& content)
{
    static const QRegularExpression reKey(QStringLiteral("^([A-Za-z_][A-Za-z0-9_]*)[ \t]"));

    QStringList keys;
    int depth = 0;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        // Only a line starting at brace depth 0 can begin a top-level
        // assignment; everything else is inside advanced_shot or a multi-line
        // profile_notes and would otherwise be mistaken for a profile key.
        if (depth == 0) {
            const QRegularExpressionMatch m = reKey.match(line);
            if (m.hasMatch() && !keys.contains(m.captured(1)))
                keys << m.captured(1);
        }
        for (const QChar c : line) {
            if (c == QLatin1Char('{')) ++depth;
            else if (c == QLatin1Char('}')) --depth;
        }
        if (depth < 0) depth = 0;  // tolerate a stray closing brace
    }
    return keys;
}

QStringList uncoveredTclKeys(const QString& content)
{
    QStringList covered = nonScalarTclKeys();
    for (const ScalarField& f : scalarFields()) {
        covered << f.tclSimple << f.tclAdvanced;
    }

    QStringList out;
    for (const QString& key : assignedTclKeys(content)) {
        if (!covered.contains(key))
            out << key;
    }
    return out;
}

QStringList keysLostByRewrite(const QJsonObject& existing, const QJsonObject& fromTcl)
{
    QStringList out;
    for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
        if (!fromTcl.contains(it.key()))
            out << it.key();
    }
    out.sort();
    return out;
}

namespace {

QJsonValue jsonValueFor(const QJsonObject& obj, const ScalarField& f)
{
    if (f.presetIndex < 0)
        return obj.value(f.canonical);
    const QJsonArray arr = obj.value(f.canonical).toArray();
    return f.presetIndex < arr.size() ? arr.at(f.presetIndex) : QJsonValue();
}

}  // namespace

QVector<ScalarDiff> compareScalars(const QString& tclContent, const QJsonObject& builtinJson)
{
    const QString profileType = extractValue(tclContent, QStringLiteral("settings_profile_type"));
    const bool advanced = isAdvancedType(profileType);

    QVector<ScalarDiff> diffs;
    for (const ScalarField& f : scalarFields()) {
        const QString tclKey = advanced ? f.tclAdvanced : f.tclSimple;
        QString raw = extractValue(tclContent, tclKey);
        if (raw.isEmpty()) {
            // Absent usually means "de1app uses its global default and so do
            // we" — nothing to compare. Where de1app documents a specific
            // absent-key value, it IS the profile's value and must be checked:
            // a built-in silently holding a 6 mL/s limiter for a profile de1app
            // runs unlimited is drift the gate has to see.
            if (!f.whenAbsent.has_value())
                continue;
            raw = QString::number(*f.whenAbsent);
        }

        const QJsonValue jv = jsonValueFor(builtinJson, f);
        if (jv.isUndefined() || jv.isNull()) {
            diffs.append({f.canonical, tclKey, raw, QStringLiteral("<absent>")});
            continue;
        }

        if (f.kind == Kind::Boolean) {
            // Both sides go through profileJsonToBool, and both check `ok`.
            // Its own contract (profile.h) says a caller COMPARING two values
            // must check it — "letting an uninterpretable value fall back to a
            // default is precisely how the parity audit compared "1" against
            // false and certified them equal". This function IS the parity
            // audit, and it previously ignored `ok` on the JSON side and used a
            // second, string-based truthiness rule on the Tcl side. Those two
            // rules disagreed on real values: "0.0" is true to one, false to
            // the other.
            bool tclOk = false, jsonOk = false;
            const bool tclOn  = profileJsonToBool(QJsonValue(raw.trimmed()), false, &tclOk);
            const bool jsonOn = profileJsonToBool(jv, false, &jsonOk);
            auto render = [](bool ok, bool on) {
                return ok ? (on ? QStringLiteral("true") : QStringLiteral("false"))
                          : QStringLiteral("<uninterpretable>");
            };
            if (!tclOk || !jsonOk || tclOn != jsonOn)
                diffs.append({f.canonical, tclKey, render(tclOk, tclOn), render(jsonOk, jsonOn)});
            continue;
        }

        bool ok = false;
        const double tclNum = raw.toDouble(&ok);
        if (!ok) {
            diffs.append({f.canonical, tclKey, raw, QStringLiteral("<unparseable tcl value>")});
            continue;
        }
        const double jsonNum = profileJsonToDouble(jv);

        // Compare the ENCODED forms rather than an ad-hoc epsilon: two values
        // that the canonical writer emits identically are the same value, by
        // definition of the format.
        const QString a = ProfileJson::enc(tclNum, f.decimals);
        const QString b = ProfileJson::enc(jsonNum, f.decimals);
        if (a != b) {
            QString canonicalLabel = f.canonical;
            if (f.presetIndex >= 0)
                canonicalLabel += QStringLiteral("[%1]").arg(f.presetIndex);
            diffs.append({canonicalLabel, tclKey, a, b});
        }
    }
    return diffs;
}

}  // namespace De1AppTcl
