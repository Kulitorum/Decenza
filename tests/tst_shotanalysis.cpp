#include <QtTest>

#include "ai/shotanalysis.h"
#include "history/shothistorystorage.h"

class tst_ShotAnalysis : public QObject {
    Q_OBJECT

private:
    static HistoryPhaseMarker phase(double time, const QString& label, int frameNumber)
    {
        HistoryPhaseMarker marker;
        marker.time = time;
        marker.label = label;
        marker.frameNumber = frameNumber;
        return marker;
    }

    static void expectSkipDetection(const QList<HistoryPhaseMarker>& phases,
                                    int expectedFrameCount,
                                    bool expected)
    {
        QCOMPARE(ShotAnalysis::detectSkipFirstFrame(phases, expectedFrameCount), expected);
    }

private slots:
    void skipFirstFrameDetection()
    {
        expectSkipDetection({}, -1, false);

        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.0, "Fill", 0),
            phase(2.3, "Pour", 1),
        }, 3, false);

        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.0, "Fill", 0),
            phase(1.4, "Pour", 1),
        }, 3, true);

        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.2, "Pour", 1),
        }, 3, true);

        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(2.0, "Pour", 1),
        }, 3, false);

        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.2, "Pour", 1),
        }, 1, false);

        // expectedFrameCount == 0: also suppresses (< 2 frames, no skip possible)
        expectSkipDetection({
            phase(0.0, "Start", 0),
            phase(0.2, "Pour", 1),
        }, 0, false);

        // FW bug: machine never executed frame 0 — first marker arrives directly at frame 1.
        // No "Start" or frame-0 entry at all. Mirrors the Tcl plugin's skipped_first_step_FW
        // case where step1_registered is never set before a non-zero frame is seen.
        expectSkipDetection({
            phase(0.0, "Pour", 1),
        }, 3, true);

        // FW bug with no expectedFrameCount (default -1, unknown profile)
        expectSkipDetection({
            phase(0.0, "Pour", 1),
        }, -1, true);
    }
};

QTEST_MAIN(tst_ShotAnalysis)
#include "tst_shotanalysis.moc"
