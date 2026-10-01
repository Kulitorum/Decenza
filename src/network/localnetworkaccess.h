#pragma once

#include <QObject>
#include <QSet>

/**
 * Android 17's local network permission (ACCESS_LOCAL_NETWORK), asked for on behalf of the
 * features that talk to the LAN.
 *
 * An app targeting API 37 on Android 17 gets no LAN traffic at all until the user grants it:
 * TCP in and out, UDP, multicast and ".local" lookups are all blocked, and a blocked TCP
 * connect surfaces as a timeout rather than an error. Each feature calls request() where it
 * starts using the network, so anything already enabled asks at launch and anything else asks
 * when the user first turns it on. Qt has no QPermission type for it (6.12), so this uses
 * QtAndroidPrivate directly.
 *
 * A no-op on every other platform and below Android 17. iOS and macOS show their own prompt.
 * Created by main() and published to QML as a singleton (contextsingletons_qml.h).
 */
class LocalNetworkAccess : public QObject
{
    Q_OBJECT

public:
    enum class Feature {
        WebServer,
        Mqtt,
        WifiScale,
        DeviceMigration,
    };
    Q_ENUM(Feature)

    explicit LocalNetworkAccess(QObject* parent = nullptr);
    ~LocalNetworkAccess() override;

    // Safe from any thread and before main() has created the instance (then a no-op).
    static void request(Feature feature);

    Q_INVOKABLE void openAppSettings();

signals:
    // Once per feature per session, when the user has refused or dismissed the prompt.
    void denied(LocalNetworkAccess::Feature feature);

private:
    void requestOnOwnThread(Feature feature);
    void onResult(bool granted);
    void reportDenied(Feature feature);

    static inline LocalNetworkAccess* s_instance = nullptr;

    bool m_requestInFlight = false;
    bool m_granted = false;
    bool m_refused = false;
    QSet<Feature> m_waiting;
    QSet<Feature> m_reportedDenied;
};
