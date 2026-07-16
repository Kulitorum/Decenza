#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>

// Helpers extracted from mcptools_shots.cpp so the pure-JSON-shape pieces
// can be unit-tested without spinning up the full MCP / DB / thread stack.

namespace McpShotsHelpers {

// Drop heavy fields (time-series, debugLog, profileJson) from a shot projection
// JSON object so it fits within typical LLM context windows. The full payload
// for a single shot is ~85K chars (mostly time-series); the summary is ~3K and
// covers every dialing/comparison use case (scalars, phaseSummaries,
// summaryLines, detectorResults, ratings).
//
// This allowlists the array-valued fields the summary carries and drops every
// other array, rather than naming the series to remove. Every ShotProjection
// series is a QVariantList of {x,y} points, so a newly added one is excluded by
// construction instead of riding along until someone remembers to denylist it —
// which is exactly how temperatureMixGoal started leaking a 200-400 point array
// into every response. Scalars are unaffected: adding one still reaches MCP
// without an edit here.
inline void stripTimeSeriesFields(QJsonObject& obj)
{
    // The only arrays the summary is meant to expose. detectorResults is a JSON
    // object, not an array, so it needs no entry.
    static const QSet<QString> summaryArrays = {
        QStringLiteral("summaryLines"),
        QStringLiteral("phases"),
        QStringLiteral("phaseSummaries"),
    };
    const QStringList keys = obj.keys();
    for (const QString& key : keys) {
        if (obj.value(key).isArray() && !summaryArrays.contains(key))
            obj.remove(key);
    }
    // Heavy, but not arrays — these have to be named.
    obj.remove(QStringLiteral("debugLog"));
    obj.remove(QStringLiteral("profileJson"));
}

// Wrap inline scalar detector outputs (pourTruncated/pourStart/pourEnd/
// peakPressureBar, skipFirstFrame, verdictCategory) into envelope objects
// so detectorResults has one consistent shape the AI can iterate over.
// Without this, "did detector X run / what's its verdict" is sometimes
// detectorResults.X.checked|direction|unstable and sometimes a flat
// top-level scalar — inconsistency the LLM has to special-case per field.
// QML reads ShotProjection directly (not this JSON), so callers there are
// unaffected.
inline void reshapeDetectorEnvelopes(QJsonObject& obj)
{
    if (!obj.contains("detectorResults"))
        return;
    QJsonObject d = obj.value("detectorResults").toObject();

    QJsonObject pour;
    pour["truncated"] = d.value("pourTruncated").toBool();
    pour["startSec"] = d.value("pourStartSec").toDouble();
    pour["endSec"] = d.value("pourEndSec").toDouble();
    // peakPressureBar is only emitted when pourTruncated; surface null
    // otherwise so the envelope shape stays uniform across shots.
    pour["peakPressureBar"] = d.contains("peakPressureBar")
        ? d.value("peakPressureBar")
        : QJsonValue(QJsonValue::Null);
    d["pour"] = pour;
    d.remove(QStringLiteral("pourTruncated"));
    d.remove(QStringLiteral("pourStartSec"));
    d.remove(QStringLiteral("pourEndSec"));
    d.remove(QStringLiteral("peakPressureBar"));

    QJsonObject skip;
    skip["detected"] = d.value("skipFirstFrame").toBool();
    d["skipFirstFrame"] = skip;

    QJsonObject verdict;
    verdict["category"] = d.value("verdictCategory").toString();
    d["verdict"] = verdict;
    d.remove(QStringLiteral("verdictCategory"));

    obj["detectorResults"] = d;
}

} // namespace McpShotsHelpers
