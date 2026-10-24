#include "shotserversettingsservicesource.h"

#include "controllers/maincontroller.h"
#include "core/settings.h"
#include "core/settings_network.h"
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
    Settings* settings, ShotServer* shotServer, MainController* controller, QObject* parent)
    : ShotServerSettingsStateSimpleSource(parent), m_settings(settings),
      m_shotServer(shotServer), m_controller(controller)
{
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
