#pragma once

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVector>

// The words a shot comparison is told in: what each input, metric, badge and stop
// reason is called, the phrases a summary is built from, and the page's own
// headings and controls. One table, so the app (which translates each key) and the
// web /compare/ page (which shows the English) say the same thing. Header-only so
// the translation registry can declare these keys without linking the comparison.
namespace ShotComparisonText {

struct Entry {
    const char* id;       // what the comparison JSON refers to: "metric.durationSec"
    const char* key;      // translation key
    const char* english;  // fallback, and the web page's text
};

inline const QVector<Entry>& entries()
{
    static const QVector<Entry> e = {
        { "input.profile",              "comparison.input.profile",          "Profile" },
        { "input.profileTemp",          "comparison.input.profileTemp",      "Profile" },
        { "input.profileSettings",      "comparison.input.profileSettings",  "Profile settings" },
        { "input.temperatureOverrideC", "comparison.input.brewTemp",         "Brew temp" },
        { "input.doseG",                "comparison.input.dose",             "Dose" },
        { "input.targetYieldG",         "comparison.input.targetYield",      "Target yield" },
        { "input.grinderSetting",       "comparison.input.grind",            "Grind" },
        { "input.rpm",                  "comparison.input.rpm",              "RPM" },
        { "input.grinder",              "comparison.input.grinder",          "Grinder" },
        { "input.burrs",                "comparison.input.burrs",            "Burrs" },
        { "input.basket",               "comparison.input.basket",           "Basket" },
        { "input.puckPrep",             "comparison.input.puckPrep",         "Puck prep" },
        { "input.bean",                 "comparison.input.bean",             "Bean" },
        { "input.roast",                "comparison.input.roast",            "Roast" },
        { "input.frozenDate",           "comparison.input.frozen",           "Frozen" },
        { "input.defrostDate",          "comparison.input.thawed",           "Thawed" },
        { "input.storageHint",          "comparison.input.storage",          "Storage" },
        { "input.openedDate",           "comparison.input.opened",           "Opened" },
        { "input.barista",              "comparison.input.barista",          "Barista" },

        { "metric.durationSec",         "comparison.metric.duration",        "Shot time" },
        { "metric.yieldG",              "comparison.metric.yield",           "Yield" },
        { "metric.ratio",               "comparison.metric.ratio",           "Ratio" },
        { "metric.firstDropSec",        "comparison.metric.firstDrop",       "First drop" },
        { "metric.peakPressureBar",     "comparison.metric.peakPressure",    "Peak pressure" },
        { "metric.meanFlowMlPerSec",    "comparison.metric.meanFlow",        "Mean flow" },
        { "metric.peakFlowMlPerSec",    "comparison.metric.peakFlow",        "Peak flow" },
        { "metric.weightFlowGPerSec",   "comparison.metric.weightFlow",      "Avg weight flow" },
        { "metric.groupTempC",          "comparison.metric.groupTemp",       "Group temp" },
        { "metric.tempSagC",            "comparison.metric.tempSag",         "Temp sag" },
        { "metric.resistance",          "comparison.metric.resistance",      "Resistance" },
        { "metric.preinfusionSec",      "comparison.metric.preinfusion",     "Preinfusion" },
        { "metric.pourSec",             "comparison.metric.pour",            "Pour" },
        { "metric.drinkTdsPct",         "comparison.metric.tds",             "TDS" },
        { "metric.drinkEyPct",          "comparison.metric.ey",              "EY" },

        { "row.stopped",                "comparison.stopped",                "Stopped" },
        { "row.rating",                 "comparison.rating",                 "Rating" },

        { "badge.pourTruncated",        "comparison.badge.pourTruncated",    "Pour truncated" },
        { "badge.channeling",           "comparison.badge.channeling",       "Channeling" },
        { "badge.grindIssue",           "comparison.badge.grindIssue",       "Grind issue" },
        { "badge.skipFirstFrame",       "comparison.badge.skipFirstFrame",   "Skipped first step" },

        { "stop.weight",                "comparison.stop.weight",            "At target weight" },
        { "stop.volume",                "comparison.stop.volume",            "At target volume" },
        { "stop.manual",                "comparison.stop.manual",            "By hand" },
        { "stop.profileEnd",            "comparison.stop.profileEnd",        "Profile finished" },
        { "stopped.weight",             "comparison.summary.stoppedWeight",  "Stopped at target weight" },
        { "stopped.volume",             "comparison.summary.stoppedVolume",  "Stopped at target volume" },
        { "stopped.manual",             "comparison.summary.stoppedManual",  "Stopped by hand" },
        { "stopped.profileEnd",         "comparison.summary.stoppedProfileEnd", "Ran to the end of the profile" },

        // %1 the metric's name, %2 the amount with its unit.
        { "phrase.longer",              "comparison.summary.longer",         "%1 %2 longer" },
        { "phrase.shorter",             "comparison.summary.shorter",        "%1 %2 shorter" },
        { "phrase.later",               "comparison.summary.later",          "%1 %2 later" },
        { "phrase.earlier",             "comparison.summary.earlier",        "%1 %2 earlier" },
        { "phrase.more",                "comparison.summary.more",           "%1 %2 more" },
        { "phrase.less",                "comparison.summary.less",           "%1 %2 less" },
        { "phrase.higher",              "comparison.summary.higher",         "%1 %2 higher" },
        { "phrase.lower",               "comparison.summary.lower",          "%1 %2 lower" },
        { "phrase.changed",             "comparison.summary.changed",        "%1 changed" },
        { "phrase.moreInputs",          "comparison.summary.moreInputs",     "%1 more" },
        { "phrase.badgeAppeared",       "comparison.summary.badgeAppeared",  "%1 appeared" },
        { "phrase.badgeGone",           "comparison.summary.badgeGone",      "%1 gone" },
        { "phrase.noNotable",           "comparison.summary.noNotable",      "No notable difference" },

        { "unit.gPerSec",               "comparison.unit.gramsPerSec",       "g/s" },
        { "unit.rpm",                   "comparison.unit.rpm",               "RPM" },

        { "ui.changed",                 "comparison.section.changed",        "What you changed" },
        { "ui.happened",                "comparison.section.happened",       "What happened" },
        { "ui.nothingChanged",          "comparison.nothingChangedAll",      "Nothing changed in your setup." },
        { "ui.sameForAll",              "comparison.sameForAll",             "Same for all" },
        { "ui.sameForBoth",             "comparison.sameForBoth",            "Same for both" },
        { "ui.sameVersion",             "comparison.sameVersion",            "(same version)" },
        { "ui.prep",                    "equipment.card.puckPrep",           "Prep: %1" },
        { "ui.profileChanged",          "comparison.profileChanged",         "Profile settings changed on %1" },
        { "ui.showMore",                "comparison.showMore",               "Show %1 more" },
        { "ui.showLess",                "comparison.showLess",               "Show less" },
        { "ui.yes",                     "common.yes",                        "Yes" },
        { "ui.no",                      "common.no",                         "No" },
        { "ui.base",                    "comparison.base",                   "Base" },
        { "ui.baseShot",                "comparison.baseShot",               "Base shot, %1" },
        { "ui.makeBase",                "comparison.makeBase",               "Make %1 the base" },
        { "ui.hideOnGraph",             "comparison.hideOnGraph",            "Hide on graph" },
        { "ui.showOnGraph",             "comparison.showOnGraph",            "Show on graph" },
        { "ui.alignPours",              "comparison.alignPours",             "Align pours" },
        { "ui.fewerCurves",             "comparison.fewerCurves",            "Fewer" },
        { "tip.fewerCurves",            "comparison.tip.fewerCurves",        "Hide the chips for curves that are turned off" },
        { "tip.moreCurves",             "comparison.tip.moreCurves",         "Show the curves that are turned off, to turn them on" },
        { "tip.phase",                  "comparison.tip.phase",              "Show or hide the %1 phase marker" },
        { "tip.alignPours",             "comparison.tip.alignPours",
          "Line up each shot's pour start with the base shot's, so a longer preinfusion does not shift the rest of its curves" },
    };
    return e;
}

// {id: {key, label}}, what QML translates from and the web page reads.
inline QJsonObject toJson()
{
    QJsonObject out;
    for (const Entry& e : entries())
        out[QLatin1String(e.id)] = QJsonObject{ { QStringLiteral("key"), QLatin1String(e.key) },
                                                { QStringLiteral("label"), QLatin1String(e.english) } };
    return out;
}

// (key, English) pairs for the translation registry, which cannot see a C++ table.
inline QVector<QPair<QString, QString>> translationStrings()
{
    QVector<QPair<QString, QString>> out;
    for (const Entry& e : entries())
        out.append({ QLatin1String(e.key), QLatin1String(e.english) });
    return out;
}

} // namespace ShotComparisonText
