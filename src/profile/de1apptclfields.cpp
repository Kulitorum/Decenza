#include "profile/de1apptclfields.h"

#include "profile/profile.h"       // profileJsonToDouble / profileJsonToBool
#include "profile/profilejson.h"   // ProfileJson::enc — the writer's precision policy

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

namespace De1AppTcl {

// The prefix match covers `settings_2c2` as well as `settings_2c`, which is correct:
// de1app treats `2c2` as a legacy alias and normalises it to `2c` before anything
// reads the type — on load via `::profile::fix_profile_type` (`de1plus/profile.tcl`,
// applied into `::settings` in `vars.tcl` ahead of `sync_from_legacy`), and on save
// via a separate inline check in `vars.tcl`'s save path rather than that same
// function. de1app's stop-target switches name
// `settings_2c` alone and so *appear* to say otherwise — they cannot, because `2c2`
// is normalised away before it reaches them. No profile in de1app, Decenza or reaprime
// carries the type, so this is a note against re-reading those switches the wrong way,
// not a live concern.
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

        // --- de1app's per-profile dose. In profile_vars (vars.tcl:3305), so de1app
        // writes it on every save, but only the Streamline skin populates it
        // (skins/Streamline/skin.tcl:2550-2556) — which is why none of the 88 shipped
        // profiles carries one and this went unnoticed. Decenza's equivalent is the
        // recommended_dose / has_recommended_dose pair; the companion flag cannot be
        // set from this table (readScalar yields a bare double), so it is set in
        // Profile::loadFromTclString.
        //
        // whenAbsent MUST stay unset. compareScalars() walks this same table and IS
        // the built-in drift gate: give this row a 0 fallback and all 88 corpus
        // profiles — none of which carries the key — would compare 0 against each
        // built-in's recommended_dose of 18.0 and fail the gate on eight files.
        { QStringLiteral("recommended_dose"), QStringLiteral("profile_grinder_dose_weight"),
          QStringLiteral("profile_grinder_dose_weight"), Kind::Number, ProfileJson::Weight },

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
        // insert_preinfusion_pause CHANGES THE SHOT in de1app: binary.tcl:880
        // prepends a 2-second pause frame when it is 1
        // (`set this_profile [concat [list $pause] $this_profile]`, :891). It is
        // compared, not waved through as unmodelled metadata — dropping it would
        // make de1app pour one more frame than we do from the same file.
        //
        // Decenza carries the flag but does NOT yet implement the pause. All
        // three stock profiles that have it set it to 0, so nothing diverges
        // today; a profile that sets it to 1 would. Comparing it is what makes
        // that visible rather than silent.
        { QStringLiteral("insert_preinfusion_pause"), QStringLiteral("insert_preinfusion_pause"),
          QStringLiteral("insert_preinfusion_pause"), Kind::Boolean },
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

namespace {

// Keys whose value is PROSE, and whose bare (unbraced, unquoted) form is therefore
// read to the end of the line rather than to the first space.
//
// This deliberately diverges from Tcl. A profile file is read with `array set`,
// which treats the whole thing as a flat key/value list, so `profile_title D-Flow / Q`
// yields profile_title -> "D-Flow" plus a stray "/" -> "Q" (verified with tclsh).
// de1app never trips this because it braces every multi-word value — all 88 of its
// profiles do, and zero carry a bare multi-word value at depth 0. Visualizer's `.tcl`
// export does NOT brace the title, so every multi-word title from that export
// truncates: "D-Flow / Q" becomes "D-Flow", "Damian's Q" becomes "Damian's".
//
// The cost of matching Tcl here is real and one-sided: the profile loses its
// slash-prefix category and so drops out of its editor group, its filename collides
// with the next download of the same family, and in DE1APP ITSELF it loses the editor
// entirely — the dispatch takes the first 8 characters of the title and compares them
// against the literal "D-Flow /":
//     set title_test [string range [ifexists ::settings(profile_title)] 0 7]
// (plugins/D_Flow_Espresso_Profile/plugin.tcl:196-197, in `proc prep`; the same test is
// repeated inline at :1144-1152). Six characters cannot satisfy it. Decenza's own writer braces properly, so re-saving repairs the file
// for every app downstream.
//
// STRICTLY PROSE ONLY. An enum or a code must NOT be added here: `beverage_type` is
// written bare across the de1app corpus in eight values (espresso x44,
// tea_portafilter x11, calibrate x5, cleaning x3, pourover x3, filter x2, manual,
// tea), and reading a malformed line whole would yield an unmatchable string and
// silently drop a classification that drives tea/pourover handling and travels on to
// Visualizer and reaprime. For those, first-token truncation is the CORRECT recovery.
// `original_profile_title` is likewise excluded: Decenza models it nowhere, so
// including it would put an unhandled key into uncoveredTclKeys().
bool isFreeTextKey(const QString& key)
{
    return key == QLatin1String("profile_title")
        || key == QLatin1String("author")
        || key == QLatin1String("profile_notes");
}

// NO COMMENT STRIPPING. An earlier revision trimmed a trailing `;#` or ` #` from a
// bare value on the theory that a hand-edited profile might carry a Tcl comment. It
// cannot: a profile file is consumed as a flat key/value list (`array set`), where no
// command is ever parsed, so neither `#` nor `;` has comment syntax anywhere in it.
// The stripping had no case to serve and one real victim — a profile titled
// `Blend #3` is written bare by Visualizer (the brace heuristic needs word/space/word
// and `#` is not a word character) and would have imported as `Blend`, reintroducing
// the exact truncation this rule exists to prevent.

}  // namespace

QList<QPair<QString, QString>> topLevelAssignments(const QString& content)
{
    // ONE definition of "a top-level assignment", used by extractValue() and
    // assignedTclKeys() alike. They used to disagree: assignedTclKeys tracked
    // brace depth so prose inside profile_notes was not mistaken for a key,
    // while extractValue searched the raw string and took the first hit in FORM
    // order (braced, then quoted, then bare) rather than position order. A note
    // reading "I tested this at maximum_flow 6" therefore beat the profile's own
    // `maximum_flow 2.5`, and the machine got 6.
    static const QRegularExpression reAssign(
        QStringLiteral("^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]+(.*)$"));

    QList<QPair<QString, QString>> out;
    int depth = 0;
    const QStringList lines = content.split(QLatin1Char('\n'));

    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);

        if (depth == 0) {
            const QRegularExpressionMatch m = reAssign.match(line);
            if (m.hasMatch()) {
                const QString key = m.captured(1);
                QString rest = m.captured(2).trimmed();

                if (rest.startsWith(QLatin1Char('{'))) {
                    // A braced value may run past this line (profile_notes,
                    // advanced_shot). Accumulate until the braces balance.
                    QString buf;
                    int d = 0;
                    qsizetype j = i;
                    bool closed = false;
                    for (; j < lines.size() && !closed; ++j) {
                        const QString& seg = (j == i) ? rest : lines.at(j);
                        if (j != i) buf += QLatin1Char('\n');
                        for (const QChar c : seg) {
                            if (c == QLatin1Char('{')) {
                                ++d;
                                if (d == 1) continue;   // drop the outermost brace
                            } else if (c == QLatin1Char('}')) {
                                --d;
                                if (d == 0) { closed = true; break; }
                            }
                            if (d >= 1) buf += c;
                        }
                    }
                    out.append({key, buf});
                    i = j - 1;   // resume after the braced value
                    continue;
                }

                if (rest.startsWith(QLatin1Char('"'))) {
                    const qsizetype end = rest.indexOf(QLatin1Char('"'), 1);
                    out.append({key, end > 0 ? rest.mid(1, end - 1) : rest.mid(1)});
                    continue;
                }

                // Bare value. Prose keys take the whole line (see isFreeTextKey);
                // everything else — numbers, enums, codes — takes the first token,
                // which is what Tcl itself would read.
                if (isFreeTextKey(key)) {
                    out.append({key, rest});
                } else {
                    out.append({key, rest.section(QRegularExpression(QStringLiteral("[ \t]")), 0, 0)});
                }
                continue;
            }
        }

        for (const QChar c : line) {
            if (c == QLatin1Char('{')) ++depth;
            else if (c == QLatin1Char('}')) --depth;
        }
        if (depth < 0) depth = 0;   // tolerate a stray closing brace
    }
    return out;
}

QString extractValue(const QString& content, const QString& varName)
{
    for (const auto& kv : topLevelAssignments(content)) {
        if (kv.first == varName)
            return kv.second;
    }
    return QString();
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

        // --- Not profile scalars. Each was traced in de1app before being
        // listed here; "we don't model it" is not on its own a reason to stop
        // comparing something, which is why this list carries evidence. ---

        // de1app UI/authoring state, with no path into frame construction:
        //   preinfusion_guarantee — set by the skin when creating a flow preset
        //     (skins/default/de1_skin_settings.tcl:2024); absent from binary.tcl
        //     and profile.tcl, so it reaches no frame.
        //   read_only_backup — de1app's saved copy of read_only for restore
        //     (vars.tcl:2944, :3325).
        //   profile_editor — records which editor plugin authored the profile
        //     (machine.tcl:495).
        QStringLiteral("preinfusion_guarantee"),
        QStringLiteral("read_only_backup"),
        QStringLiteral("profile_editor"),

        // de1app/DSx bean and grinder metadata. Decenza models these in its own
        // bean and equipment records, not on the profile, so they are outside
        // what a profile comparison can meaningfully say anything about.
        QStringLiteral("bean_brand"),      QStringLiteral("bean_type"),
        QStringLiteral("grinder_model"),   QStringLiteral("grinder_setting"),
        QStringLiteral("grinder_dose_weight"),
        // profile_grinder_setting is de1app's PER-PROFILE grind setting, written
        // alongside profile_grinder_dose_weight by the Streamline skin. Unlike the
        // dose it is not mapped: Decenza models grind settings on equipment and
        // recipes, and pinning a grind string to a profile would invent an
        // association de1app does not make either — its own value is a free-text
        // note the skin copies to and from the global setting.
        QStringLiteral("profile_grinder_setting"),

        // Legacy spellings, one profile each (flow_calibration.tcl), and NOT in
        // de1app's own profile_vars list (vars.tcl:3305) — so de1app never
        // writes them and its save would drop them too. Dead on both sides.
        QStringLiteral("maximum_flow_range"),
        QStringLiteral("maximum_pressure_range"),
    };
    return kKeys;
}

QStringList assignedTclKeys(const QString& content)
{
    QStringList keys;
    for (const auto& kv : topLevelAssignments(content)) {
        if (!keys.contains(kv.first))
            keys << kv.first;
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
