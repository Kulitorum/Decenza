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

    if (m_firstSample) {
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
    return 0;
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    kern_return_t kr = task_info(mach_task_self(), TASK_VM_INFO,
                                 reinterpret_cast<task_info_t>(&info), &count);
    if (kr == KERN_SUCCESS)
        return info.phys_footprint;
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
                break;
            }
        }
    }
    return 0;
#else
    return 0;
#endif
}

int MemoryMonitor::countQObjects() const
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

        // Also walk root objects and their trees
        const auto roots = m_engine->rootObjects();
        for (auto* root : roots) {
            if (!root) continue;
            seen.insert(root);
            const auto rootChildren = root->findChildren<QObject*>();
            for (auto* obj : rootChildren)
                seen.insert(obj);
        }
    }

    return seen.size();
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

    QJsonObject root;
    root["current"] = current;
    root["peak"] = peak;
    root["startup"] = startup;
    root["uptimeMinutes"] = static_cast<qint64>(m_uptime.elapsed() / 60000);
    root["samples"] = samplesArr;

    return root;
}
