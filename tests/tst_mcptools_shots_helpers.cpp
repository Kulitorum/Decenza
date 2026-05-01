#include <QTest>
#include <QJsonObject>
#include <QJsonValue>
#include "mcp/mcptools_shots_helpers.h"

using McpShotsHelpers::reshapeDetectorEnvelopes;

class TstMcpToolsShotsHelpers : public QObject
{
    Q_OBJECT

private slots:
    void noDetectorResults_isNoOp();
    void cleanShot_pourEnvelope_peakPressureBarIsNull();
    void truncatedShot_pourEnvelope_peakPressureBarPopulated();
    void skipFirstFrame_isWrappedAsObject();
    void verdictCategory_isWrappedAsObject();
    void oldKeysAreRemoved();
    void wrappedDetectorsAreUntouched();
};

// Minimal helper to build the inline-scalar shape that the serializer
// produces in src/history/shothistorystorage_serialize.cpp:204-209.
static QJsonObject makeRawDetectorResults(bool truncated = false,
                                          bool skip = false,
                                          const QString& verdict = QStringLiteral("clean"))
{
    QJsonObject d;
    d["pourTruncated"] = truncated;
    d["pourStartSec"] = 1.871;
    d["pourEndSec"] = 26.281;
    if (truncated) d["peakPressureBar"] = 11.4;
    d["skipFirstFrame"] = skip;
    d["verdictCategory"] = verdict;
    return d;
}

void TstMcpToolsShotsHelpers::noDetectorResults_isNoOp()
{
    QJsonObject obj{{"id", 42}};
    reshapeDetectorEnvelopes(obj);
    QCOMPARE(obj.value("id").toInt(), 42);
    QVERIFY(!obj.contains("detectorResults"));
}

void TstMcpToolsShotsHelpers::cleanShot_pourEnvelope_peakPressureBarIsNull()
{
    QJsonObject obj{{"detectorResults", makeRawDetectorResults(/*truncated=*/false)}};
    reshapeDetectorEnvelopes(obj);

    const QJsonObject pour = obj.value("detectorResults").toObject().value("pour").toObject();
    QCOMPARE(pour.value("truncated").toBool(), false);
    QCOMPARE(pour.value("startSec").toDouble(), 1.871);
    QCOMPARE(pour.value("endSec").toDouble(), 26.281);
    // Uniform shape: peakPressureBar is always present, null when not truncated.
    QVERIFY(pour.contains("peakPressureBar"));
    QVERIFY(pour.value("peakPressureBar").isNull());
}

void TstMcpToolsShotsHelpers::truncatedShot_pourEnvelope_peakPressureBarPopulated()
{
    QJsonObject obj{{"detectorResults", makeRawDetectorResults(/*truncated=*/true)}};
    reshapeDetectorEnvelopes(obj);

    const QJsonObject pour = obj.value("detectorResults").toObject().value("pour").toObject();
    QCOMPARE(pour.value("truncated").toBool(), true);
    QVERIFY(!pour.value("peakPressureBar").isNull());
    QCOMPARE(pour.value("peakPressureBar").toDouble(), 11.4);
}

void TstMcpToolsShotsHelpers::skipFirstFrame_isWrappedAsObject()
{
    QJsonObject obj{{"detectorResults", makeRawDetectorResults(false, /*skip=*/true)}};
    reshapeDetectorEnvelopes(obj);

    const QJsonValue skipVal = obj.value("detectorResults").toObject().value("skipFirstFrame");
    QVERIFY(skipVal.isObject());
    QCOMPARE(skipVal.toObject().value("detected").toBool(), true);
}

void TstMcpToolsShotsHelpers::verdictCategory_isWrappedAsObject()
{
    QJsonObject obj{{"detectorResults",
                     makeRawDetectorResults(true, false, QStringLiteral("puckTruncated"))}};
    reshapeDetectorEnvelopes(obj);

    const QJsonObject verdict = obj.value("detectorResults").toObject().value("verdict").toObject();
    QCOMPARE(verdict.value("category").toString(), QStringLiteral("puckTruncated"));
}

void TstMcpToolsShotsHelpers::oldKeysAreRemoved()
{
    QJsonObject obj{{"detectorResults", makeRawDetectorResults(true, false)}};
    reshapeDetectorEnvelopes(obj);

    const QJsonObject d = obj.value("detectorResults").toObject();
    // None of the old top-level scalar keys should remain.
    QVERIFY(!d.contains("pourTruncated"));
    QVERIFY(!d.contains("pourStartSec"));
    QVERIFY(!d.contains("pourEndSec"));
    QVERIFY(!d.contains("peakPressureBar"));
    QVERIFY(!d.contains("verdictCategory"));
    // skipFirstFrame is repurposed as an object envelope, so it stays as a
    // key but must now be an object, not a bool.
    QVERIFY(d.contains("skipFirstFrame"));
    QVERIFY(d.value("skipFirstFrame").isObject());
}

void TstMcpToolsShotsHelpers::wrappedDetectorsAreUntouched()
{
    QJsonObject grind;
    grind["checked"] = true;
    grind["direction"] = "onTarget";
    QJsonObject channeling;
    channeling["checked"] = false;

    QJsonObject d = makeRawDetectorResults();
    d["grind"] = grind;
    d["channeling"] = channeling;

    QJsonObject obj{{"detectorResults", d}};
    reshapeDetectorEnvelopes(obj);

    const QJsonObject reshaped = obj.value("detectorResults").toObject();
    QCOMPARE(reshaped.value("grind").toObject().value("direction").toString(),
             QStringLiteral("onTarget"));
    QCOMPARE(reshaped.value("channeling").toObject().value("checked").toBool(), false);
}

QTEST_APPLESS_MAIN(TstMcpToolsShotsHelpers)

#include "tst_mcptools_shots_helpers.moc"
