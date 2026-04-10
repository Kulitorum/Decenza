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
    }
};

QTEST_MAIN(tst_ShotAnalysis)
#include "tst_shotanalysis.moc"
