#pragma once

#include <QObject>
#include <QString>

class QTimer;

/**
 * On-demand mDNS discovery for the Half Decent Scale on the LAN.
 *
 * Only probes when explicitly told to via probe(). Does no background work.
 *
 * On a successful resolution emits scaleFound(hostname, resolvedAddress)
 * followed by probeFinished(). On timeout / NotFound emits only
 * probeFinished(). Calling probe() while a lookup is in flight starts a
 * new lookup; the previous lookup's result will be dropped. On non-Android
 * the previous QHostInfo lookup is synchronously aborted. On Android the
 * lookup runs on a worker thread via MdnsResolver (a direct mDNS A-record
 * query, since the OS resolver doesn't speak mDNS); the worker cannot be
 * synchronously joined, so its late result is dropped via a generation check.
 */
class WifiScaleDiscovery : public QObject {
    Q_OBJECT

public:
    explicit WifiScaleDiscovery(QObject* parent = nullptr);
    ~WifiScaleDiscovery() override;

    static constexpr int kDefaultTimeoutMs = 2000;
    static constexpr const char* kDefaultHostname = "hds.local";

    /**
     * Start an mDNS lookup for the HDS. Cancels any previous in-flight
     * lookup. Emits scaleFound() on success and probeFinished() in all
     * cases.
     */
    Q_INVOKABLE void probe(const QString& hostname = QStringLiteral("hds.local"),
                           int timeoutMs = kDefaultTimeoutMs);

    /**
     * True iff a probe is currently in flight.
     */
    bool isProbing() const {
#ifdef Q_OS_ANDROID
        return m_androidInFlight;
#else
        return m_lookupId != -1;
#endif
    }

signals:
    void scaleFound(const QString& hostname, const QString& resolvedAddress);
    void probeFinished();

private:
    void cancelInFlight();

    int m_lookupId = -1;  // QHostInfo lookup id on non-Android paths
    QString m_currentHostname;
    QTimer* m_timeoutTimer = nullptr;

#ifdef Q_OS_ANDROID
    // Android uses NsdManager via a JNI helper on a worker thread instead of
    // QHostInfo (the stock resolver doesn't do mDNS). A monotonically
    // increasing generation lets us drop late callbacks after cancel/timeout.
    bool m_androidInFlight = false;
    int m_androidGeneration = 0;
#endif
};
