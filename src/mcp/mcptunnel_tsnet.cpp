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

void McpTunnelTsnet::applyUpdate(quint64 epoch, State state, const QString& authUrl,
                                 const QString& certDomain, const QString& errorMsg)
{
    Q_ASSERT(thread() == QThread::currentThread());  // main thread only
    // Drop updates from a superseded worker generation (post-stop / restart).
    if (epoch != m_epoch.load())
        return;

    if (!errorMsg.isEmpty())
        m_lastError = errorMsg;
    else if (state == Running || state == Stopped)
        m_lastError.clear();  // don't leave a stale error after recovery
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
    applyUpdate(m_epoch.load(), Error, QString(), QString(),
                QStringLiteral("This build does not include Tailscale support (ENABLE_TSNET off)"));
#else
    // Reap a worker that has already exited (e.g. bring-up failed with Error) so
    // a re-start is possible; only refuse if one is genuinely still running.
    if (m_worker) {
        if (m_worker->isFinished()) {
            delete m_worker;
            m_worker = nullptr;
        } else {
            return;
        }
    }
    m_stopRequested = false;
    m_stateDir = stateDir;
    const quint64 epoch = ++m_epoch;
    applyUpdate(epoch, Starting, QString(), QString(), QString());

    m_worker = QThread::create([this, epoch, stateDir, hostname, localPort]() {
        runWorker(epoch, stateDir, hostname, localPort);
    });
    m_worker->setObjectName(QStringLiteral("tsnet-worker"));
    m_worker->start();
#endif
}

void McpTunnelTsnet::runWorker(quint64 epoch, QString stateDir, QString hostname, quint16 localPort)
{
#ifdef DECENZA_ENABLE_TSNET
    auto post = [this, epoch](State st, const QString& au, const QString& cd, const QString& err) {
        QMetaObject::invokeMethod(this, [this, epoch, st, au, cd, err]() {
            applyUpdate(epoch, st, au, cd, err);
        }, Qt::QueuedConnection);
    };
    // Interruptible sleep so stop() (which sets m_stopRequested) is honoured
    // within ~100 ms — the old single msleep(5000) could outrun stop()'s join
    // timeout and get the still-running QThread deleted (a fatal abort).
    auto sleepInterruptible = [this](int totalMs) {
        for (int slept = 0; slept < totalMs && !m_stopRequested.load(); slept += 100)
            QThread::msleep(100);
    };
    // The worker exclusively owns the libtailscale handle and closes it on exit.
    auto closeHandle = [this]() {
        const int h = m_handle.exchange(-1);
        if (h >= 0)
            tailscale_close(h);
    };

    QDir().mkpath(stateDir);

    const int sd = tailscale_new();
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

    // Cancel bring-up if stop() arrived between tailscale_new and here.
    if (m_stopRequested.load()) { closeHandle(); return; }

    tailscale_set_logfd(sd, -1);  // discard tsnet's own logging
    // set_dir/set_hostname run once at bring-up; a failure here is always real
    // (unwritable state dir, rejected hostname → wrong Funnel FQDN) — surface it.
    if (tailscale_set_dir(sd, stateDir.toUtf8().constData()) != 0) {
        post(Error, QString(), QString(),
             QStringLiteral("Tailscale state dir rejected: ") + errmsg());
        closeHandle();
        return;
    }
    if (tailscale_set_hostname(sd, hostname.toUtf8().constData()) != 0) {
        post(Error, QString(), QString(),
             QStringLiteral("Tailscale hostname rejected: ") + errmsg());
        closeHandle();
        return;
    }

    // Non-blocking bring-up so we can surface the login URL while it comes up.
    if (tailscale_start(sd) != 0) {
        post(Error, QString(), QString(), errmsg());
        closeHandle();
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
                // the connector URL / "Active" (funnel may not be granted yet).
                post(Running, QString(), domain, QString());
            }
        } else if (state == QLatin1String("NeedsLogin")
                   || state == QLatin1String("NeedsMachineAuth")) {
            post(NeedsLogin, authUrl(), QString(), QString());
        } else if (state == QLatin1String("Stopped")
                   || state == QLatin1String("InUseOtherUser")) {
            // Non-transient: these won't advance on their own — surface, not "Starting".
            post(Error, QString(), QString(),
                 QStringLiteral("Tailscale backend state: ") + state);
        } else {
            // NoState / Starting / empty (query error) — normal during bring-up.
            post(Starting, QString(), QString(), QString());
        }
        // Poll faster while coming up; ease off (still re-applying funnel) once up.
        sleepInterruptible(state == QLatin1String("Running") ? 5000 : 1500);
    }

    // Stop requested (or loop exited): the worker owns closing the handle so the
    // main thread never races a tailscale_* call still running here.
    closeHandle();
#else
    Q_UNUSED(epoch)
    Q_UNUSED(stateDir)
    Q_UNUSED(hostname)
    Q_UNUSED(localPort)
#endif
}

void McpTunnelTsnet::stop()
{
#ifdef DECENZA_ENABLE_TSNET
    // Invalidate any queued updates from the current worker before we tear down.
    const quint64 epoch = ++m_epoch;
    m_stopRequested = true;
    if (m_worker) {
        // The worker honours m_stopRequested within ~100 ms (interruptible sleep),
        // then closes the handle and returns. Only delete once it has actually
        // finished — deleting a still-running QThread is fatal. On the rare
        // timeout (a libtailscale call stalled), leak the thread rather than crash.
        if (m_worker->wait(12000)) {
            delete m_worker;
            m_worker = nullptr;
        } else {
            qWarning() << "McpTunnelTsnet: worker did not stop within timeout; leaking it";
        }
    }
    applyUpdate(epoch, Stopped, QString(), QString(), QString());
#endif
}

void McpTunnelTsnet::wipeState()
{
    const QString dir = m_stateDir;
    stop();
    if (!dir.isEmpty() && !QDir(dir).removeRecursively())
        qWarning() << "McpTunnelTsnet: failed to wipe tsnet state dir" << dir;
}
