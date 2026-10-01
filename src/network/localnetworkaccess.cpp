#include "localnetworkaccess.h"

#include "core/networklogging.h"

#include <QMetaObject>
#include <QThread>

#include <utility>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QFuture>
#include <QJniObject>
#include <QtCore/private/qandroidextras_p.h>
#endif

namespace {

#ifdef Q_OS_ANDROID
constexpr int kAndroid17 = 37;
const QString kPermission = QStringLiteral("android.permission.ACCESS_LOCAL_NETWORK");
#endif

QString featureName(LocalNetworkAccess::Feature feature)
{
    switch (feature) {
    case LocalNetworkAccess::Feature::WebServer: return QStringLiteral("web server");
    case LocalNetworkAccess::Feature::Mqtt: return QStringLiteral("MQTT");
    case LocalNetworkAccess::Feature::WifiScale: return QStringLiteral("WiFi scale");
    case LocalNetworkAccess::Feature::DeviceMigration: return QStringLiteral("device migration");
    }
    return QStringLiteral("unknown");
}

} // namespace

LocalNetworkAccess::LocalNetworkAccess(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
}

LocalNetworkAccess::~LocalNetworkAccess()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void LocalNetworkAccess::request(Feature feature)
{
    LocalNetworkAccess* instance = s_instance;
    if (!instance)
        return;
    if (QThread::currentThread() == instance->thread()) {
        instance->requestOnOwnThread(feature);
        return;
    }
    QMetaObject::invokeMethod(instance, [instance, feature] { instance->requestOnOwnThread(feature); },
                              Qt::QueuedConnection);
}

void LocalNetworkAccess::requestOnOwnThread(Feature feature)
{
#ifdef Q_OS_ANDROID
    if (m_granted || QNativeInterface::QAndroidApplication::sdkVersion() < kAndroid17)
        return;

    // Ready immediately (qandroidextras.cpp:1180-1188), and it picks up a grant the user made
    // in Android Settings after refusing the prompt.
    if (QtAndroidPrivate::checkPermission(kPermission).result() == QtAndroidPrivate::Authorized) {
        onResult(true);
        return;
    }
    // Ask once per session. Features keep calling (the WiFi scale on every reconnect attempt),
    // and after a refusal each call would otherwise prompt again.
    if (m_refused) {
        reportDenied(feature);
        return;
    }

    m_waiting.insert(feature);
    if (m_requestInFlight)
        return;
    m_requestInFlight = true;
    NETWORK_INFO_STDERR("LocalNetworkAccess",
        QStringLiteral("Requesting local network permission for the %1").arg(featureName(feature)));
    QtAndroidPrivate::requestPermission(kPermission).then(this, [this](QtAndroidPrivate::PermissionResult result) {
        onResult(result == QtAndroidPrivate::Authorized);
    });
#else
    Q_UNUSED(feature);
#endif
}

void LocalNetworkAccess::onResult(bool granted)
{
    m_requestInFlight = false;
    const QSet<Feature> waiting = std::exchange(m_waiting, {});

    if (granted) {
        if (!m_granted)
            NETWORK_INFO_STDERR("LocalNetworkAccess", QStringLiteral("Local network permission granted"));
        m_granted = true;
        return;
    }
    m_refused = true;
    for (Feature feature : waiting)
        reportDenied(feature);
}

void LocalNetworkAccess::reportDenied(Feature feature)
{
    if (m_reportedDenied.contains(feature))
        return;
    m_reportedDenied.insert(feature);
    NETWORK_WARN_STDERR("LocalNetworkAccess",
        QStringLiteral("Local network permission refused — the %1 cannot reach the LAN until "
                       "\"Nearby devices\" is allowed for Decenza in Android Settings")
            .arg(featureName(feature)));
    emit denied(feature);
}

void LocalNetworkAccess::openAppSettings()
{
#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;
    const QJniObject packageName = context.callMethod<jstring>("getPackageName");
    const QJniObject uri = QJniObject::callStaticMethod<jobject>(
            "android/net/Uri", "fromParts",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;",
            QJniObject::fromString(QStringLiteral("package")).object<jstring>(),
            packageName.object<jstring>(), nullptr);
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;Landroid/net/Uri;)V",
                      QJniObject::fromString(QStringLiteral("android.settings.APPLICATION_DETAILS_SETTINGS"))
                              .object<jstring>(),
                      uri.object());
    intent.callMethod<jobject>("addFlags", "(I)Landroid/content/Intent;", 0x10000000);  // FLAG_ACTIVITY_NEW_TASK
    if (intent.isValid())
        context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
#endif
}
