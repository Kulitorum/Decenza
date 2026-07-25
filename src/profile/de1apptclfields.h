#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <utility>

// de1app's profile-level scalar vocabulary, plus the type-dependent rule that
// decides which of two spellings is authoritative.
//
// This exists so that Profile::loadFromTclString() and the drift gate in
// tools/profile_sync resolve the SAME .tcl key for the same canonical field.
// The two disagreeing is not hypothetical: a comparison written against the
// wrong spelling once inflated the built-in drift list from 4 rows to 60 and
// sent a whole day of analysis down the wrong path.
//
// The rule and its de1app citations live in docs/CLAUDE_MD/RECIPE_PROFILES.md,
// "Reading a de1app .tcl: which spelling wins depends on settings_profile_type".
namespace De1AppTcl {

// settings_2c / settings_2c2 — the types de1app converts with
// settings_to_advanced_list(), which leaves the _advanced fields alone.
// settings_2a/2b overwrite them from their plain counterparts first.
bool isAdvancedType(const QString& profileType);

enum class Kind {
    Number,   // string-encoded double in the canonical JSON
    Boolean,  // JSON bool, or "0"/"1" string
};

struct ScalarField {
    QString canonical;      // Decenza / DE1 v2 JSON key
    QString tclSimple;      // authoritative .tcl key for settings_2a / settings_2b
    QString tclAdvanced;    // authoritative .tcl key for settings_2c / settings_2c2
    Kind    kind = Kind::Number;
    int     decimals = 2;   // writer precision; two values that encode alike are equal
    int     presetIndex = -1;  // >= 0: element `presetIndex` of temperature_presets
    // What de1app uses when a profile omits the key, where that differs from
    // Profile's own member default. de1app sets these explicitly before reading
    // a legacy profile (de1plus/profile.tcl:513-519, "Disable limits by
    // default"). Unset means Profile's default already agrees.
    std::optional<double> whenAbsent;

    // Spelled out rather than left as an aggregate so the table below can stop
    // at the fields an entry actually needs (-Werror=missing-field-initializers
    // would otherwise demand all seven on every row).
    ScalarField(QString canonicalKey, QString simpleKey, QString advancedKey,
                Kind valueKind = Kind::Number, int decimalPlaces = 2,
                int presetIdx = -1, std::optional<double> absentValue = std::nullopt)
        : canonical(std::move(canonicalKey)), tclSimple(std::move(simpleKey)),
          tclAdvanced(std::move(advancedKey)), kind(valueKind),
          decimals(decimalPlaces), presetIndex(presetIdx), whenAbsent(absentValue) {}
};

// Every profile-level scalar the import preserves and the gate compares.
const QVector<ScalarField>& scalarFields();

// The authoritative .tcl key for `canonical` under `profileType`. Empty when the
// field map does not cover `canonical`.
QString tclKeyFor(const QString& canonical, const QString& profileType);

// Read one Tcl scalar. Handles `name {braced}`, `name "quoted"` and `name bare`.
// The \b guard stops preinfusion_time matching flow_profile_preinfusion_time —
// those are two different de1app editor settings, not aliases.
QString extractValue(const QString& content, const QString& varName);

enum class ReadStatus {
    Absent,     // the .tcl does not carry the key; `value` is the fallback
    Parsed,     // the .tcl carries it and it read as a number
    Malformed,  // the .tcl carries it and it does NOT read as a number
};

struct ScalarRead {
    double     value = 0.0;
    ReadStatus status = ReadStatus::Absent;
    QString    raw;      // the .tcl text, for a diagnostic
    QString    tclKey;   // the key actually consulted, after the type rule
};

// Read a canonical numeric field from a `.tcl`: the file's own value if it
// carries the authoritative spelling, else de1app's documented absent-key
// value, else `fallback`.
//
// Reading a scalar means resolving a key AND deciding what absence means, and
// both answers live in the table above — so the reader gets them from here
// rather than restating them.
//
// `status` distinguishes ABSENT from MALFORMED, and callers must act on the
// difference. Absent is ordinary: de1app falls back and so do we. Malformed is
// not, and collapsing the two substitutes a number that changes the shot with
// no diagnostic — `maximum_pressure 9,5` (a locale decimal comma) would read as
// "no pressure limit", and `final_desired_shot_weight n/a` would switch
// stop-at-weight on at the 36 g default.
ScalarRead readScalar(const QString& content, const QString& canonical,
                      const QString& profileType, double fallback);

// Top-level .tcl keys that are deliberately NOT part of the scalar comparison,
// each for a stated reason (handled elsewhere, or derived). Kept explicit so an
// uncompared key reads as a decision rather than an oversight.
const QStringList& nonScalarTclKeys();

// Every top-level key assigned in `content`, at brace depth 0 — so keys inside
// `advanced_shot {...}` or a multi-line `profile_notes {...}` are not counted.
QStringList assignedTclKeys(const QString& content);

// Keys `content` assigns that are neither compared nor listed as non-scalar.
// A non-empty result means the field map has a hole; reporting it is the whole
// point, since an incomplete map silently narrows the comparison instead of
// failing.
QStringList uncoveredTclKeys(const QString& content);

// Keys the built-in holds that a .tcl-derived rewrite would not reproduce.
//
// A shipped built-in is supposed to BE its de1app source — the standing promise
// is that the same profile makes the same coffee in every app — so a re-sync
// deliberately does not preserve anything extra. This reports what a rewrite
// would drop so the claim is checked rather than assumed; it must come back
// empty, and a non-empty result is a stop, not something to merge around.
QStringList keysLostByRewrite(const QJsonObject& existing, const QJsonObject& fromTcl);

struct ScalarDiff {
    QString canonical;
    QString tclKey;
    QString tclValue;
    QString jsonValue;
};

// Compare a raw de1app .tcl against a Decenza profile JSON object, field by
// field, applying the type-dependent rule. Deliberately reads the .tcl directly
// rather than through Profile::loadFromTclString(): routing both sides through
// the reader would make the gate structurally unable to see a reader bug, which
// is the class of bug it exists to catch.
//
// A key absent from the .tcl is skipped — de1app falls back to its global
// default there, and so do we.
QVector<ScalarDiff> compareScalars(const QString& tclContent, const QJsonObject& builtinJson);

}  // namespace De1AppTcl
