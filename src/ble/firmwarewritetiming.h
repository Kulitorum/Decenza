#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <chrono>

// Passive, bounded firmware-write measurements. Times are monotonic nanoseconds;
// callers can supply a synthetic clock in tests. Never schedules or releases work.
class FirmwareWriteTiming {
public:
    static qint64 nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void dispatched(const QByteArray& packet, qint64 now, qsizetype depth) {
        if (!m_running) {
            m_running = true;
            m_windowStart = now;
        }
        if (m_packet == packet) {
            ++m_retries;
        } else {
            if (m_lastFinished >= 0) m_dispatchGap.add(now - m_lastFinished);
            m_lastFinished = -1;
            m_packet = packet;
        }
        m_dispatched = now;
        m_depthMax = std::max(m_depthMax, depth);
    }

    void issued(qint64 now) { m_issue.add(now - m_dispatched); }

    bool acknowledged(const QByteArray& packet, qint64 signalAt, qint64 handledAt) {
        if (!m_running || m_packet.isEmpty() || m_packet != packet) return false;
        // signalAt is when OUR direct signal tap ran, not an HCI/radio timestamp.
        m_ack.add(signalAt - m_dispatched);
        m_delivery.add(handledAt - signalAt);
        ++m_acked;
        ++m_totalAcked;
        return true;
    }

    void callbackFinished(qint64 enteredAt, qint64 releasedAt, qint64 consumersFinishedAt, qint64 finishedAt) {
        m_release.add(releasedAt - enteredAt);
        m_consumers.add(consumersFinishedAt - releasedAt);
        m_drain.add(finishedAt - consumersFinishedAt);
        m_lastFinished = finishedAt;
        m_packet.clear();
    }

    bool reportDue() const { return m_totalAcked == 64 || m_acked >= 1024; }

    QString report(const QString& reason) {
        if (!m_running || (m_acked == 0 && m_retries == 0 && m_issue.count == 0)) return {};
        const double seconds = std::max<qint64>(0, m_lastFinished - m_windowStart) / 1e9;
        const QString result = QStringLiteral(
            "firmware BLE timing reason=%1 acked=%2 totalAcked=%3 retries=%4 pendingMax=%5 "
            "rate=%6 packets/s (16 payload bytes/packet); avg/max ms: "
            "writeToQtSignal=%7 qtDelivery=%8 issueCall=%9 queueRelease=%10 "
            "ackConsumers=%11 drainCheck=%12 nextDispatchGap=%13")
            .arg(reason).arg(m_acked).arg(m_totalAcked).arg(m_retries).arg(m_depthMax)
            .arg(seconds > 0 ? m_acked / seconds : 0, 0, 'f', 2)
            .arg(m_ack.text(), m_delivery.text(), m_issue.text(), m_release.text(),
                 m_consumers.text(), m_drain.text(), m_dispatchGap.text());
        m_windowStart = m_lastFinished >= 0 ? m_lastFinished : m_dispatched;
        m_acked = m_retries = 0;
        m_depthMax = 0;
        m_ack = m_delivery = m_issue = m_release = m_consumers = m_drain = m_dispatchGap = {};
        return result;
    }

    void reset() { *this = FirmwareWriteTiming{}; }

private:
    struct Sample {
        qint64 count = 0;
        qint64 sum = 0;
        qint64 max = 0;
        void add(qint64 ns) {
            ns = std::max<qint64>(0, ns);
            ++count;
            sum += ns;
            max = std::max(max, ns);
        }
        QString text() const {
            return QStringLiteral("%1/%2").arg(count ? sum / double(count) / 1e6 : 0, 0, 'f', 3)
                .arg(max / 1e6, 0, 'f', 3);
        }
    };
    bool m_running = false;
    QByteArray m_packet;
    qint64 m_dispatched = 0;
    qint64 m_windowStart = 0;
    qint64 m_lastFinished = -1;
    qint64 m_acked = 0;
    qint64 m_totalAcked = 0;
    qint64 m_retries = 0;
    qsizetype m_depthMax = 0;
    Sample m_ack, m_delivery, m_issue, m_release, m_consumers, m_drain, m_dispatchGap;
};
