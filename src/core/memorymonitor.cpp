#include "memorymonitor.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QRegularExpression>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
#include <mach/mach.h>
#endif

#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#include <QFile>
#include <QTextStream>
#endif

MemoryMonitor::MemoryMonitor(QObject* parent)
    : QObject(parent)
{
    m_uptime.start();

    m_timer.setInterval(60000);
    connect(&m_timer, &QTimer::timeout, this, &MemoryMonitor::takeSample);
    m_timer.start();

    // Take initial sample
    takeSample();
}

void MemoryMonitor::takeSample()
{
    quint64 rss = readRss();
    int objCount = countQObjects();

    m_lastRss = rss;
    m_lastQObjectCount = objCount;

    if (m_firstSample && rss > 0) {
        m_startupRss = rss;
        m_firstSample = false;
    }

    if (rss > m_peakRss)
        m_peakRss = rss;

    MemorySample sample;
    sample.timestampMs = QDateTime::currentMSecsSinceEpoch();
    sample.rssBytes = rss;
    sample.qobjectCount = objCount;

    if (m_samples.size() >= MAX_SAMPLES)
        m_samples.removeFirst();
    m_samples.append(sample);

    double rssMB = rss / (1024.0 * 1024.0);
    double peakMB = m_peakRss / (1024.0 * 1024.0);
    qDebug("[Memory] RSS: %.1f MB, QObjects: %d, peak: %.1f MB", rssMB, objCount, peakMB);

    emit sampleTaken();
}

quint64 MemoryMonitor::readRss() const
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return pmc.WorkingSetSize;
    qWarning("[Memory] GetProcessMemoryInfo failed");
    return 0;
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    kern_return_t kr = task_info(mach_task_self(), TASK_VM_INFO,
                                 reinterpret_cast<task_info_t>(&info), &count);
    if (kr == KERN_SUCCESS)
        return info.phys_footprint;  // phys_footprint is the "real" memory cost (compressed + swapped), more accurate than resident_size
    qWarning("[Memory] task_info failed: %d", kr);
    return 0;
#elif defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    QFile f("/proc/self/status");
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("VmRSS:")) {
                // Format: "VmRSS:    12345 kB"
                QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    bool ok;
                    quint64 kb = parts[1].toULongLong(&ok);
                    if (ok)
                        return kb * 1024;
                }
                qWarning("[Memory] Failed to parse VmRSS line: %s", qPrintable(line));
                break;
            }
        }
    } else {
        qWarning("[Memory] Failed to open /proc/self/status");
    }
    return 0;
#else
    return 0;
#endif
}

QSet<QObject*> MemoryMonitor::collectAllQObjects() const
{
    // Walk both QApplication and QML engine trees to get a meaningful count.
    // Most QObjects are parented under the engine (QML items), not the app.
    QSet<QObject*> seen;

    auto* app = QCoreApplication::instance();
    if (app) {
        const auto appChildren = app->findChildren<QObject*>();
        for (auto* obj : appChildren)
            seen.insert(obj);
        seen.insert(app);
    }

    if (m_engine) {
        const auto engineChildren = m_engine->findChildren<QObject*>();
        for (auto* obj : engineChildren)
            seen.insert(obj);
        seen.insert(m_engine);

        const auto roots = m_engine->rootObjects();
        for (auto* root : roots) {
            if (!root) continue;
            seen.insert(root);
            const auto rootChildren = root->findChildren<QObject*>();
            for (auto* obj : rootChildren)
                seen.insert(obj);
        }
    }

    return seen;
}

int MemoryMonitor::countQObjects()
{
    const auto all = collectAllQObjects();

    // Build per-class counts
    m_prevClassCounts = m_classCounts;
    m_classCounts.clear();
    for (auto* obj : all) {
        const char* name = obj->metaObject()->className();
        m_classCounts[QString::fromLatin1(name)]++;
    }

    // Capture baseline on first sample after engine is set (when QML tree is populated)
    if (!m_baselineCaptured && m_engine) {
        m_baselineClassCounts = m_classCounts;
        m_baselineCaptured = true;
    }

    // Log classes with biggest growth since last sample
    if (!m_prevClassCounts.isEmpty()) {
        QVector<QPair<QString, int>> deltas;
        for (auto it = m_classCounts.constBegin(); it != m_classCounts.constEnd(); ++it) {
            int prev = m_prevClassCounts.value(it.key(), 0);
            int delta = it.value() - prev;
            if (delta != 0)
                deltas.append({it.key(), delta});
        }
        // Also check classes that disappeared
        for (auto it = m_prevClassCounts.constBegin(); it != m_prevClassCounts.constEnd(); ++it) {
            if (!m_classCounts.contains(it.key()))
                deltas.append({it.key(), -it.value()});
        }

        if (!deltas.isEmpty()) {
            // Sort by absolute delta descending
            std::sort(deltas.begin(), deltas.end(), [](const auto& a, const auto& b) {
                return qAbs(a.second) > qAbs(b.second);
            });
            QStringList parts;
            int limit = qMin(deltas.size(), 5);
            for (int i = 0; i < limit; ++i) {
                const auto& d = deltas[i];
                parts << QStringLiteral("%1%2 %3")
                    .arg(d.second > 0 ? "+" : "").arg(d.second).arg(d.first);
            }
            qDebug("[Memory] QObject deltas: %s", qPrintable(parts.join(", ")));
        }
    }

    return all.size();
}

double MemoryMonitor::currentRssMB() const
{
    return m_lastRss / (1024.0 * 1024.0);
}

double MemoryMonitor::peakRssMB() const
{
    return m_peakRss / (1024.0 * 1024.0);
}

double MemoryMonitor::startupRssMB() const
{
    return m_startupRss / (1024.0 * 1024.0);
}

QJsonObject MemoryMonitor::toJson() const
{
    QJsonObject current;
    current["rssBytes"] = static_cast<qint64>(m_lastRss);
    current["rssMB"] = qRound(currentRssMB() * 10) / 10.0;
    current["qobjectCount"] = m_lastQObjectCount;

    QJsonObject peak;
    peak["rssBytes"] = static_cast<qint64>(m_peakRss);
    peak["rssMB"] = qRound(peakRssMB() * 10) / 10.0;

    QJsonObject startup;
    startup["rssBytes"] = static_cast<qint64>(m_startupRss);
    startup["rssMB"] = qRound(startupRssMB() * 10) / 10.0;

    QJsonArray samplesArr;
    for (const auto& s : m_samples) {
        QJsonObject obj;
        obj["t"] = s.timestampMs;
        obj["rss"] = qRound(s.rssBytes / (1024.0 * 1024.0) * 10) / 10.0;
        obj["obj"] = s.qobjectCount;
        samplesArr.append(obj);
    }

    // Per-class breakdown: sort by count descending, return top 30
    QVector<QPair<QString, int>> sorted;
    sorted.reserve(m_classCounts.size());
    for (auto it = m_classCounts.constBegin(); it != m_classCounts.constEnd(); ++it)
        sorted.append({it.key(), it.value()});
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    // Delta is vs. baseline (first sample after engine set) — shows growth since startup
    const auto& deltaBase = m_baselineCaptured ? m_baselineClassCounts : m_prevClassCounts;

    QJsonArray classesArr;
    int classLimit = qMin(sorted.size(), 30);
    for (int i = 0; i < classLimit; ++i) {
        QJsonObject cls;
        cls["name"] = sorted[i].first;
        cls["count"] = sorted[i].second;
        int base = deltaBase.value(sorted[i].first, 0);
        cls["delta"] = sorted[i].second - base;
        classesArr.append(cls);
    }

    QJsonObject root;
    root["current"] = current;
    root["peak"] = peak;
    root["startup"] = startup;
    root["uptimeMinutes"] = static_cast<qint64>(m_uptime.elapsed() / 60000);
    root["samples"] = samplesArr;
    root["topClasses"] = classesArr;

    return root;
}
