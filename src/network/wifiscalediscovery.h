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
 * probeFinished(). Calling probe() while a lookup is in flight cancels
 * the previous lookup before starting a new one.
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
    bool isProbing() const { return m_lookupId != -1; }

signals:
    void scaleFound(const QString& hostname, const QString& resolvedAddress);
    void probeFinished();

private:
    void cancelInFlight();

    int m_lookupId = -1;
    QString m_currentHostname;
    QTimer* m_timeoutTimer = nullptr;
};
