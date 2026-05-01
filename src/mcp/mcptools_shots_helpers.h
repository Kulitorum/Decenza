#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

// Helpers extracted from mcptools_shots.cpp so the pure-JSON-shape pieces
// can be unit-tested without spinning up the full MCP / DB / thread stack.

namespace McpShotsHelpers {

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
