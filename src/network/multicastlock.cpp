#include "multicastlock.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#endif

namespace MulticastLock {
namespace {

QMutex g_mutex;
int g_holders = 0;

#ifdef Q_OS_ANDROID
QJniObject g_lock;

// Whether the "could not take it" warning has already been logged. Discovery
// runs repeatedly on a reconnect ladder, and a device with no WifiManager would
// otherwise fill the ring buffer that field diagnosis reads with one line per
// attempt.
bool g_warned = false;

// Caller holds g_mutex.
void acquireLocked()
{
    if (g_lock.isValid())
        return;

    // The APPLICATION context, not the activity. getSystemService(WIFI_SERVICE)
    // off an Activity leaks it, and the activity is null when the app has no
    // foreground window — which is precisely when a background reconnect runs.
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        if (!g_warned) {
            qWarning() << "[MulticastLock] no Android context — multicast reception "
                          "will be filtered by the Wi-Fi driver";
            g_warned = true;
        }
        return;
    }

    QJniObject service = QJniObject::fromString(QStringLiteral("wifi"));
    QJniObject wifiManager = context.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
        service.object<jstring>());
    if (!wifiManager.isValid()) {
        if (!g_warned) {
            qWarning() << "[MulticastLock] WifiManager unavailable — multicast "
                          "reception will be filtered by the Wi-Fi driver";
            g_warned = true;
        }
        return;
    }

    QJniObject tag = QJniObject::fromString(QStringLiteral("Decenza"));
    QJniObject lock = wifiManager.callObjectMethod(
        "createMulticastLock",
        "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;",
        tag.object<jstring>());
    if (!lock.isValid()) {
        if (!g_warned) {
            qWarning() << "[MulticastLock] createMulticastLock failed";
            g_warned = true;
        }
        return;
    }

    // Reference counting is OURS, not Android's. Android's own counter would
    // work, but then acquire/release have to be perfectly paired across every
    // caller forever, and a single missed release leaves the radio filter off
    // for the rest of the session with nothing to show for it. Counting Holders
    // here means the invariant is enforced by a destructor.
    lock.callMethod<void>("setReferenceCounted", "(Z)V", jboolean(false));
    lock.callMethod<void>("acquire");
    g_lock = lock;
    g_warned = false;  // a later failure is news again
    qDebug() << "[MulticastLock] acquired";
}

// Caller holds g_mutex.
void releaseLocked()
{
    if (!g_lock.isValid())
        return;
    if (g_lock.callMethod<jboolean>("isHeld"))
        g_lock.callMethod<void>("release");
    g_lock = QJniObject();
    qDebug() << "[MulticastLock] released";
}
#else
void acquireLocked() {}
void releaseLocked() {}
#endif

}  // namespace

Holder::Holder()
{
    QMutexLocker lock(&g_mutex);
    if (++g_holders == 1)
        acquireLocked();
}

Holder::~Holder()
{
    QMutexLocker lock(&g_mutex);
    if (--g_holders == 0)
        releaseLocked();
}

bool isHeld()
{
    QMutexLocker lock(&g_mutex);
#ifdef Q_OS_ANDROID
    return g_lock.isValid();
#else
    return false;
#endif
}

int holderCount()
{
    QMutexLocker lock(&g_mutex);
    return g_holders;
}

}  // namespace MulticastLock
