#include "shotserversettingsservicesource.h"

#include "controllers/maincontroller.h"
#include "core/settings.h"
#include "core/settings_network.h"
#include "network/remoteaccess.h"
#include "network/shotserver.h"
#include "serviceoperation.h"
#include "shotserversettingspolicy.h"

#include <QMetaType>
#include <QtGlobal>

namespace {
QVariantMap failure(const QString& reason)
{
    return {{QStringLiteral("success"), false}, {QStringLiteral("reason"), reason}};
}
}

NativeShotServerSettingsSource::NativeShotServerSettingsSource(
    Settings* settings, ShotServer* shotServer, MainController* controller,
    RemoteAccess* remoteAccess, QObject* parent)
    : ShotServerSettingsStateSimpleSource(parent), m_settings(settings),
      m_shotServer(shotServer), m_controller(controller), m_remoteAccess(remoteAccess)
{
    if (m_remoteAccess) {
        connect(m_remoteAccess, &RemoteAccess::statusChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
        connect(m_remoteAccess, &RemoteAccess::tailscaleSetupNeededChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
        // Not the URLs themselves (they carry access material): whether each is
        // ready, and the revision, so a card sees a rotation that leaves both ready.
        connect(m_remoteAccess, &RemoteAccess::webUrlChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
        connect(m_remoteAccess, &RemoteAccess::loginUrlChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
        connect(m_remoteAccess, &RemoteAccess::urlRevisionChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
    }
    if (m_shotServer) {
        connect(m_shotServer, &ShotServer::runningChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
        connect(m_shotServer, &ShotServer::urlChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
        connect(m_shotServer, &ShotServer::hasTotpSecretChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
        connect(m_shotServer, &QObject::destroyed, this, [this] {
            m_shotServer = nullptr;
            refreshSnapshot();
        });
    }
    if (m_settings) {
        connect(m_settings->network(), &SettingsNetwork::shotServerEnabledChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
        connect(m_settings->network(), &SettingsNetwork::webSecurityEnabledChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
        connect(m_settings->network(), &SettingsNetwork::remoteWebEnabledChanged, this,
                &NativeShotServerSettingsSource::refreshSnapshot);
        connect(m_settings, &QObject::destroyed, this, [this] {
            m_settings = nullptr;
            refreshSnapshot();
        });
    }
    refreshSnapshot();
}

void NativeShotServerSettingsSource::refresh()
{
    refreshSnapshot();
}

void NativeShotServerSettingsSource::refreshSnapshot()
{
    QVariantMap value;
    if (m_settings && m_shotServer) {
        const auto* network = m_settings->network();
        value.insert(QStringLiteral("running"), m_shotServer->isRunning());
        value.insert(QStringLiteral("url"), m_shotServer->url().left(2048));
        value.insert(QStringLiteral("hasTotpSecret"), m_shotServer->hasStoredTotpSecret());
        value.insert(QStringLiteral("shotServerEnabled"), network->shotServerEnabled());
        value.insert(QStringLiteral("webSecurityEnabled"), network->webSecurityEnabled());
        value.insert(QStringLiteral("remoteWebEnabled"), network->remoteWebEnabled());
    }
    value.insert(QStringLiteral("tunnelAvailable"), RemoteAccess::tunnelAvailable());
    if (m_remoteAccess) {
        // The shared node's state. URL material (the safe URL, the login URL)
        // never goes in the snapshot; requestRemoteWebUrls returns it.
        value.insert(QStringLiteral("remoteAccessStatus"), m_remoteAccess->statusString());
        value.insert(QStringLiteral("remoteAccessStatusDetail"), m_remoteAccess->statusDetail().left(512));
        value.insert(QStringLiteral("tailscaleSetupNeeded"), m_remoteAccess->tailscaleSetupNeeded());
        value.insert(QStringLiteral("tailscaleLoginPending"), !m_remoteAccess->loginUrl().isEmpty());
        value.insert(QStringLiteral("remoteWebReady"), !m_remoteAccess->webUrl().isEmpty());
        value.insert(QStringLiteral("remoteUrlRevision"), m_remoteAccess->urlRevision());
    }
    setSnapshot(value);
    setReady(m_settings && m_shotServer);
}

QVariantMap NativeShotServerSettingsSource::executePrivate(
    QString requestId, QString operation, QVariantMap arguments)
{
    if (!ShotServerSettingsPolicy::validRequestId(requestId)) return failure(QStringLiteral("invalidRequest"));
    if (!m_settings || !m_shotServer) return failure(QStringLiteral("unavailable"));
    if (!ShotServerSettingsPolicy::validCommand(operation, arguments))
        return failure(QStringLiteral("invalidArguments"));

    if (operation == QLatin1String("generateTotpSetup")) {
        if (!arguments.isEmpty() || !m_settings->network()->shotServerEnabled()
            || !m_settings->network()->webSecurityEnabled() || m_shotServer->hasStoredTotpSecret())
            return failure(QStringLiteral("unavailable"));
        const QVariantMap setup = m_shotServer->generateTotpSetup();
        const QString secret = setup.value(QStringLiteral("secret")).toString();
        const QString uri = setup.value(QStringLiteral("uri")).toString();
        if (secret.isEmpty() || uri.isEmpty() || uri.size() > 2048)
            return failure(QStringLiteral("setupFailed"));
        return {{QStringLiteral("success"), true},
                {QStringLiteral("result"), QVariantMap{{QStringLiteral("secret"), secret},
                                                       {QStringLiteral("uri"), uri}}}};
    }
    if (operation == QLatin1String("completeTotpSetup")) {
        if (!m_settings->network()->shotServerEnabled()
            || !m_settings->network()->webSecurityEnabled())
            return failure(QStringLiteral("unavailable"));
        const QString secret = arguments.value(QStringLiteral("secret")).toString();
        const QString code = arguments.value(QStringLiteral("code")).toString();
        const bool success = m_shotServer->completeTotpSetup(secret, code);
        refreshSnapshot();
        return {{QStringLiteral("success"), success},
                {QStringLiteral("reason"), success ? QString() : QStringLiteral("invalidCode")}};
    }
    if (operation == QLatin1String("resetTotpSecret")) {
        if (!arguments.isEmpty() || !m_settings->network()->webSecurityEnabled())
            return failure(QStringLiteral("unavailable"));
        m_shotServer->resetTotpSecret();
        refreshSnapshot();
        return {{QStringLiteral("success"), true}};
    }
    if (operation == QLatin1String("requestRemoteWebUrls")) {
        if (!m_remoteAccess)
            return failure(QStringLiteral("unavailable"));
        return {{QStringLiteral("success"), true},
                {QStringLiteral("result"), QVariantMap{
                    {QStringLiteral("safeUrl"), m_remoteAccess->webUrl()},
                    {QStringLiteral("loginUrl"), m_remoteAccess->loginUrl()}}}};
    }
    if (operation == QLatin1String("rotateRemoteWebToken")) {
        if (!m_remoteAccess)
            return failure(QStringLiteral("unavailable"));
        m_remoteAccess->rotateWebToken();
        return {{QStringLiteral("success"), true},
                {QStringLiteral("result"), QVariantMap{
                    {QStringLiteral("safeUrl"), m_remoteAccess->webUrl()}}}};
    }
    if (operation == QLatin1String("forgetTailscale")) {
        if (!m_remoteAccess)
            return failure(QStringLiteral("unavailable"));
        const bool wiped = m_remoteAccess->forgetTailscale();
        return {{QStringLiteral("success"), wiped},
                {QStringLiteral("reason"), wiped ? QString() : QStringLiteral("forgetFailed")}};
    }
    if (operation == QLatin1String("factoryResetAndQuit")) {
        if (!arguments.isEmpty() || !m_controller)
            return failure(QStringLiteral("unavailable"));
        // Called only after the existing explicit destructive confirmation.
        // Canonical host path stops the server, closes storage, wipes settings,
        // then queues application quit. No browser-specific quit is requested.
        m_controller->factoryResetAndQuit();
        return {{QStringLiteral("success"), true}};
    }
    return failure(QStringLiteral("unknownOperation"));
}
