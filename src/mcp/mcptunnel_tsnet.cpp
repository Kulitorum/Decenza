#include "mcptunnel_tsnet.h"

#include <QThread>
#include <QDir>
#include <QMetaObject>
#include <QDebug>

#ifdef DECENZA_ENABLE_TSNET
extern "C" {
#include "tailscale.h"
}
#endif

McpTunnelTsnet::McpTunnelTsnet(QObject* parent)
    : QObject(parent)
{
}

McpTunnelTsnet::~McpTunnelTsnet()
{
    stop();
}

bool McpTunnelTsnet::isAvailable()
{
#ifdef DECENZA_ENABLE_TSNET
    return true;
#else
    return false;
#endif
}

void McpTunnelTsnet::applyUpdate(State state, const QString& authUrl,
                                 const QString& certDomain, const QString& errorMsg)
{
    // Main thread.
    if (!errorMsg.isEmpty())
        m_lastError = errorMsg;
    if (m_authUrl != authUrl) {
        m_authUrl = authUrl;
        emit authUrlChanged();
    }
    if (m_certDomain != certDomain) {
        m_certDomain = certDomain;
        emit certDomainChanged();
    }
    if (m_state != state) {
        m_state = state;
        emit stateChanged();
    }
}

void McpTunnelTsnet::start(const QString& stateDir, const QString& hostname, quint16 localPort)
{
#ifndef DECENZA_ENABLE_TSNET
    Q_UNUSED(stateDir)
    Q_UNUSED(hostname)
    Q_UNUSED(localPort)
    applyUpdate(Error, QString(), QString(),
                QStringLiteral("This build does not include Tailscale support (ENABLE_TSNET off)"));
#else
    if (m_worker)
        return;  // already running — stop() first
    m_stopRequested = false;
    m_stateDir = stateDir;
    applyUpdate(Starting, QString(), QString(), QString());

    m_worker = QThread::create([this, stateDir, hostname, localPort]() {
        runWorker(stateDir, hostname, localPort);
    });
    m_worker->setObjectName(QStringLiteral("tsnet-worker"));
    m_worker->start();
#endif
}

void McpTunnelTsnet::runWorker(QString stateDir, QString hostname, quint16 localPort)
{
#ifdef DECENZA_ENABLE_TSNET
    auto post = [this](State st, const QString& au, const QString& cd, const QString& err) {
        QMetaObject::invokeMethod(this, [this, st, au, cd, err]() {
            applyUpdate(st, au, cd, err);
        }, Qt::QueuedConnection);
    };

    QDir().mkpath(stateDir);

    int sd = tailscale_new();
    if (sd < 0) {
        post(Error, QString(), QString(), QStringLiteral("tailscale_new failed"));
        return;
    }
    m_handle = sd;

    char buf[1024];
    auto errmsg = [&]() -> QString {
        return tailscale_errmsg(sd, buf, sizeof(buf)) == 0
                   ? QString::fromUtf8(buf) : QStringLiteral("unknown error");
    };
    auto backendState = [&]() -> QString {
        return tailscale_get_backend_state(sd, buf, sizeof(buf)) == 0
                   ? QString::fromUtf8(buf) : QString();
    };
    auto authUrl = [&]() -> QString {
        return tailscale_get_auth_url(sd, buf, sizeof(buf)) == 0
                   ? QString::fromUtf8(buf) : QString();
    };
    auto certDomain = [&]() -> QString {
        return tailscale_get_cert_domain(sd, buf, sizeof(buf)) == 0
                   ? QString::fromUtf8(buf) : QString();
    };

    tailscale_set_logfd(sd, -1);  // discard tsnet's own logging
    tailscale_set_dir(sd, stateDir.toUtf8().constData());
    tailscale_set_hostname(sd, hostname.toUtf8().constData());

    // Non-blocking bring-up so we can surface the login URL while it comes up.
    if (tailscale_start(sd) != 0) {
        post(Error, QString(), QString(), errmsg());
        return;
    }

    bool funnelLogged = false;
    QString lastState;
    while (!m_stopRequested.load()) {
        const QString state = backendState();
        if (state != lastState) {
            qInfo() << "McpTunnelTsnet: backend state ->" << state;
            lastState = state;
        }

        if (state == QLatin1String("Running")) {
            const QString domain = certDomain();
            if (domain.isEmpty()) {
                // Node up but no DNS/cert domain yet — keep waiting.
                post(Starting, QString(), QString(), QString());
            } else {
                // (Re-)apply the Funnel serve config every cycle. SetServeConfig
                // is idempotent, and re-applying is what lets a Funnel grant made
                // WHILE the app is running take effect without a restart — the
                // one-shot approach got permanently stuck if Funnel wasn't yet
                // authorised when we first reached Running.
                if (tailscale_enable_funnel_to_localhost_plaintext_http1(sd, localPort) != 0)
                    qWarning() << "McpTunnelTsnet: enable funnel failed:" << errmsg();
                if (!funnelLogged) {
                    funnelLogged = true;
                    qInfo() << "McpTunnelTsnet: Funnel configured for" << domain;
                }
                // Report the FQDN. This is NOT proof of public reachability —
                // McpRemoteAccess probes the real Funnel URL before it surfaces
                // the connector URL / "Active".
                post(Running, QString(), domain, QString());
            }
        } else if (state == QLatin1String("NeedsLogin")
                   || state == QLatin1String("NeedsMachineAuth")) {
            post(NeedsLogin, authUrl(), QString(), QString());
        } else {
            post(Starting, QString(), QString(), QString());
        }
        // Poll faster while coming up; ease off (still re-applying funnel) once up.
        QThread::msleep(state == QLatin1String("Running") ? 5000 : 1500);
    }
#else
    Q_UNUSED(stateDir)
    Q_UNUSED(hostname)
    Q_UNUSED(localPort)
#endif
}

void McpTunnelTsnet::stop()
{
#ifdef DECENZA_ENABLE_TSNET
    m_stopRequested = true;
    if (m_worker) {
        // The worker breaks out of its poll loop within one interval; wait a bit
        // longer than that. (No event loop runs on it, so quit() is a no-op.)
        m_worker->wait(6000);
        delete m_worker;
        m_worker = nullptr;
    }
    const int sd = m_handle.exchange(-1);
    if (sd >= 0)
        tailscale_close(sd);
    applyUpdate(Stopped, QString(), QString(), QString());
#endif
}

void McpTunnelTsnet::wipeState()
{
    const QString dir = m_stateDir;
    stop();
    if (!dir.isEmpty())
        QDir(dir).removeRecursively();
}
