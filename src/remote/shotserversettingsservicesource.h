#pragma once

#include "rep_shotserversettingsservice_source.h"

#include <QPointer>

class MainController;
class Settings;
class ShotServer;

class NativeShotServerSettingsSource final : public ShotServerSettingsStateSimpleSource
{
    Q_OBJECT
public:
    NativeShotServerSettingsSource(Settings* settings, ShotServer* shotServer,
                                   MainController* controller, QObject* parent = nullptr);
    void refresh() override;
    QVariantMap executePrivate(QString requestId, QString operation,
                               QVariantMap arguments) override;
private:
    void refreshSnapshot();
    QPointer<Settings> m_settings;
    QPointer<ShotServer> m_shotServer;
    QPointer<MainController> m_controller;
};
