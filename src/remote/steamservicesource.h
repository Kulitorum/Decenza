#pragma once

#include "rep_steamservice_source.h"
#include "core/steamgraphupdate.h"

#include <QMetaObject>
#include <QPointer>
#include <QVariantMap>

class DE1Device;
class MainController;
class MachineState;
class ScaleDeviceProxy;
class Settings;
class SteamDataModel;
class SteamHealthTracker;
class SteamPageEditor;
class PreparationController;

class NativeSteamServiceSource final : public SteamStateSimpleSource
{
    Q_OBJECT

public:
    NativeSteamServiceSource(MainController* controller, Settings* settings,
                             MachineState* machineState, DE1Device* device,
                             ScaleDeviceProxy* scale, QObject* parent = nullptr);

    void setIdentity(const QString& packageId, const QString& hostId);
    void refreshSnapshot();
    QVariantMap currentGraphSnapshot() override;
    QVariantMap execute(QString operation, QVariantMap fields,
                        QString expectedRevision) override;

private:
    void scheduleRefresh();
    void scheduleSettingsRefresh();
    void scheduleWarningRefresh();
    void scheduleGraphRefresh();
    QVariantMap failure(const QString& message) const;
    QVariantMap editorOperation(const QVariantMap& fields,
                                const QString& expectedRevision);
    bool contextMatches(const QString& expectedRevision) const;

    QPointer<MainController> m_controller;
    QPointer<Settings> m_settings;
    QPointer<MachineState> m_machineState;
    QPointer<DE1Device> m_device;
    QPointer<ScaleDeviceProxy> m_scale;
    QPointer<SteamDataModel> m_graph;
    QPointer<SteamHealthTracker> m_health;
    QPointer<SteamPageEditor> m_editor;
    QPointer<PreparationController> m_preparation;
    bool m_refreshQueued = false;
    bool m_settingsDirty = true;
    bool m_warningsDirty = true;
    bool m_graphSnapshotDirty = true;
    SteamGraphUpdate::Publisher m_graphPublisher;
};
