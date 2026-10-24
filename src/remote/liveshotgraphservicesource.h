#pragma once

#include "rep_liveshotgraphservice_source.h"

#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

class MachineState;
class ProfileManager;
class ShotDataModel;

class NativeLiveShotGraphSource final : public LiveShotGraphStateSimpleSource
{
    Q_OBJECT
public:
    NativeLiveShotGraphSource(ShotDataModel* model, ProfileManager* profiles,
                              MachineState* machineState, QObject* parent = nullptr);

    void setIdentity(const QString& packageId, const QString& hostId);
    void requestSnapshot(QString requestId) override;

private:
    QVariantMap captureSnapshot(QString* error = nullptr) const;
    void resetEpoch();
    void publishDelta(QVariantMap fields);
    void publishModelChanges();
    bool payloadWithinLimit(const QVariantMap& payload) const;

    QPointer<ShotDataModel> m_model;
    QPointer<ProfileManager> m_profiles;
    QPointer<MachineState> m_machineState;
    QVariantMap m_lastMetadata;
    QVariantList m_lastMarkers;
    QVector<qsizetype> m_pressureGoalCursors;
    QVector<qsizetype> m_flowGoalCursors;
    qsizetype m_temperatureGoalCursor = 0;
    qsizetype m_temperatureMixGoalCursor = 0;
    qsizetype m_lastPortalCount = 0;
    qsizetype m_lastPressure = 0;
    qsizetype m_lastFlow = 0;
    qsizetype m_lastTemperature = 0;
    qsizetype m_lastTemperatureMix = 0;
    qsizetype m_lastWeight = 0;
    qsizetype m_lastWeightFlow = 0;
    qsizetype m_lastResistance = 0;
    qsizetype m_lastConductance = 0;
    qsizetype m_lastDarcyResistance = 0;
    bool m_resetQueued = false;
};
