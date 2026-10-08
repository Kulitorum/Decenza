#include "permissionrequests.h"

#include <QCoreApplication>
#include <QPointer>
#include <QQueue>
#include <QThread>

#ifdef Q_OS_ANDROID
#include <QFuture>
#include <QtCore/private/qandroidextras_p.h>
#endif

#include <utility>

namespace {

// Each entry issues one request and must call finishCurrent() when it is answered.
QQueue<std::function<void()>> s_pending;
bool s_busy = false;

void startNext()
{
    if (s_busy || s_pending.isEmpty())
        return;
    s_busy = true;
    s_pending.dequeue()();
}

void finishCurrent()
{
    s_busy = false;
    startNext();
}

void enqueue(std::function<void()> start)
{
    Q_ASSERT(QThread::isMainThread());
    s_pending.enqueue(std::move(start));
    startNext();
}

} // namespace

void PermissionRequests::request(const QPermission& permission, QObject* context,
                                 std::function<void(const QPermission&)> callback)
{
    enqueue([permission, guard = QPointer<QObject>(context), ownedCallback = std::move(callback)] {
        // qApp, not `context`, as the receiver: the queue must advance even if `context` is gone.
        qApp->requestPermission(permission, qApp, [guard, ownedCallback](const QPermission& answered) {
            if (guard)
                ownedCallback(answered);
            finishCurrent();
        });
    });
}

#ifdef Q_OS_ANDROID
void PermissionRequests::requestAndroid(const QString& permission, QObject* context,
                                        std::function<void(std::optional<bool>)> callback)
{
    enqueue([permission, guard = QPointer<QObject>(context), ownedCallback = std::move(callback)] {
        // The continuation takes the future, not the value: a cancelled request finishes with no
        // result, and a value-taking continuation reads result 0 regardless (qfuture_impl.h:626-633).
        QtAndroidPrivate::requestPermission(permission).then(qApp,
            [guard, ownedCallback](QFuture<QtAndroidPrivate::PermissionResult> future) {
                const QList<QtAndroidPrivate::PermissionResult> results = future.results();
                if (guard) {
                    ownedCallback(results.isEmpty()
                                 ? std::nullopt
                                 : std::optional<bool>(results.first() == QtAndroidPrivate::Authorized));
                }
                finishCurrent();
            });
    });
}
#endif
