#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>

#include "logcollapse.h"

class QQmlApplicationEngine;

struct MemorySample {
    qint64 timestampMs;
    quint64 rssBytes;
    int qobjectCount;
};

class MemoryMonitor : public QObject {
    Q_OBJECT

    Q_PROPERTY(double currentRssMB READ currentRssMB NOTIFY sampleTaken)
    Q_PROPERTY(double peakRssMB READ peakRssMB NOTIFY sampleTaken)
    Q_PROPERTY(int qobjectCount READ qobjectCount NOTIFY sampleTaken)

public:
    explicit MemoryMonitor(QObject* parent = nullptr);

    void setEngine(QQmlApplicationEngine* engine) { m_engine = engine; }

    double currentRssMB() const;
    Q_INVOKABLE double liveRssMB() const;  // Fresh read (not cached) — for diagnostic logging

    // True when this build carries sanitizer instrumentation or is an
    // unoptimised debug build. Both inflate RSS substantially — measured on
    // macOS, an ASan+UBSan Debug build starts at ~463 MB against ~180 MB for
    // Release, roughly 2.6x — so any threshold tuned against a Release build
    // will misfire here. Consumers should scale, not branch on the platform.
    Q_PROPERTY(bool instrumentedBuild READ instrumentedBuild CONSTANT)
    bool instrumentedBuild() const;
    double peakRssMB() const;
    double startupRssMB() const;
    int qobjectCount() const { return m_lastQObjectCount; }
    quint64 currentRssBytes() const { return m_lastRss; }
    quint64 peakRssBytes() const { return m_peakRss; }
    quint64 startupRssBytes() const { return m_startupRss; }

    QJsonObject toJson() const;
    QString toSummaryString() const;

signals:
    void sampleTaken();

private slots:
    void onSampleTimerTick();

private:
    quint64 readRss() const;
    int countQObjects();
    QSet<QObject*> collectAllQObjects() const;

    QQmlApplicationEngine* m_engine = nullptr;
    QTimer m_timer;
    QElapsedTimer m_uptime;

    QVector<MemorySample> m_samples;
    static constexpr int MAX_SAMPLES = 1440;  // 24 hours at 60s interval

    quint64 m_lastRss = 0;
    quint64 m_peakRss = 0;
    quint64 m_startupRss = 0;
    int m_lastQObjectCount = 0;
    bool m_firstSample = true;

    // Collapses the per-sample log line while a plateau holds — see onSampleTimerTick(). Sampling
    // itself is untouched; this only decides whether the sample is worth a line.
    // Periodic: samples for the process lifetime, so there is no run end and nothing to flush.
    LogCollapse m_logCollapse{10 * 60 * 1000};
    // RSS as of the last line PRINTED — the anchor the 5 MB band is measured from, so a value
    // sitting on a fixed bucket edge cannot oscillate across it. Negative until the first line.
    double m_lastLoggedRssMB = -1.0;
    // Peak RSS as of the last line printed BECAUSE of a new peak. Distinct from m_peakRss, which
    // tracks the true peak exactly for peakRssMB() and the JSON snapshot: this one only decides
    // whether a peak is worth a line, so a peak ratcheting up through jitter stays quiet. Zero
    // until the first line.
    quint64 m_lastLoggedPeakRss = 0;

    // Per-class QObject tracking
    QHash<QString, int> m_classCounts;          // Current snapshot
    QHash<QString, int> m_prevClassCounts;       // Previous snapshot (for per-tick delta log)
    QHash<QString, int> m_baselineClassCounts;   // First snapshot after engine set (for growth-since-startup)
    bool m_baselineCaptured = false;

#ifndef QT_NO_DEBUG
    // Scan visible QML Text items for emoji codepoints that trigger CopyEmojiImage crash on macOS
    void scanForEmojiText();
    QSet<QString> m_reportedEmojiTexts;  // Avoid duplicate warnings
#endif
};
