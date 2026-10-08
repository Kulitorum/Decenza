#pragma once

#include <QString>
#include <QVariantMap>
#include <QVector>

// What each ProfileFieldDelta kind is called to a user, plus the name of an unnamed
// step. One table for ProfileDialInDiffBlock.qml (which translates each key) and the
// web /compare/ page (which shows the English). Header-only so the translation
// registry can declare the keys without linking the profile code.
namespace ProfileDialInText {

struct Entry {
    const char* id;       // a ProfileFieldDelta kind, or "step"
    const char* key;      // translation key
    const char* english;
};

inline const QVector<Entry>& entries()
{
    static const QVector<Entry> e = {
        { "targetWeight",        "profilediff.field.targetWeight",        "Yield" },
        { "targetVolume",        "profilediff.field.targetVolume",        "Target volume" },
        { "maximumPressure",     "profilediff.field.maximumPressure",     "Pressure limit" },
        { "maximumFlow",         "profilediff.field.maximumFlow",         "Flow limit" },
        { "minimumPressure",     "profilediff.field.minimumPressure",     "Minimum pressure" },
        { "tankTemperature",     "profilediff.field.tankTemperature",     "Tank preheat" },
        { "espressoTemperature", "profilediff.field.espressoTemperature", "Brew temperature" },
        { "recommendedDose",     "profilediff.field.recommendedDose",     "Dose" },
        { "temperature",         "profilediff.field.temperature",         "Temperature" },
        { "pressure",            "profilediff.field.pressure",            "Pressure" },
        { "flow",                "profilediff.field.flow",                "Flow" },
        { "volume",              "profilediff.field.volume",              "Volume cap" },
        { "exitPressureOver",    "profilediff.field.exitPressureOver",    "Exit above pressure" },
        { "exitPressureUnder",   "profilediff.field.exitPressureUnder",   "Exit below pressure" },
        { "exitFlowOver",        "profilediff.field.exitFlowOver",        "Exit above flow" },
        { "exitFlowUnder",       "profilediff.field.exitFlowUnder",       "Exit below flow" },
        { "exitWeight",          "profilediff.field.exitWeight",          "Exit at weight" },
        { "maxFlowOrPressure",   "profilediff.field.limiter",             "Limiter" },
        { "name",                "profilediff.field.name",                "Step name" },
        { "step",                "profilediff.step_number",               "Step %1" },
    };
    return e;
}

// {id: {key, label}}
inline QVariantMap labelMap()
{
    QVariantMap out;
    for (const Entry& e : entries())
        out.insert(QLatin1String(e.id), QVariantMap{ { QStringLiteral("key"), QLatin1String(e.key) },
                                                     { QStringLiteral("label"), QLatin1String(e.english) } });
    return out;
}

} // namespace ProfileDialInText
