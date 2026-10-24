#pragma once

#include <QByteArray>
#include <QList>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QtGlobal>

class QJsonObject;

// Bounded JSON projection of the existing native ShotDataModel. This class is
// deliberately a pure serializer: it neither owns nor mutates machine/model
// state, so its limit and shape contract can be tested without the app stack.
class RemoteShotGraphSnapshot final
{
public:
    static constexpr qsizetype MaximumEntriesPerCurve = 6000;
    static constexpr qsizetype MaximumPhaseMarkers = 512;
    static constexpr qsizetype MaximumResponseBytes = 2 * 1024 * 1024;

    struct PhaseMarker {
        double time = 0.0;
        QString label;
        int frameNumber = -1;
        bool isFlowMode = false;
        QString transitionReason;
        QString transitionSuffix;
    };

    struct Input {
        QString hostId;
        quint64 revision = 0;
        double rawTime = 0.0;
        double weightAtStop = 0.0;
        double finalWeight = 0.0;
        double profileTargetWeight = 0.0;
        double machineTargetWeight = 0.0;
        bool hasProfileTargetWeight = false;
        bool hasMachineTargetWeight = false;
        QList<PhaseMarker> phaseMarkers;

        QVector<QPointF> pressure;
        QVector<QPointF> flow;
        QVector<QPointF> temperature;
        QVector<QPointF> temperatureMix;
        QVector<QPointF> weight;
        QVector<QPointF> weightFlowRate;
        QVector<QPointF> resistance;
        QVector<QPointF> conductance;
        QVector<QPointF> darcyResistance;

        // Goal curves use paired NaNs as the model's segment separator. They
        // serialize as a JSON null between the surrounding [x,y] pairs.
        QVector<QPointF> pressureGoal;
        QVector<QPointF> flowGoal;
        QVector<QPointF> temperatureGoal;
        QVector<QPointF> temperatureMixGoal;
    };

    struct Result {
        int httpStatus = 200;
        QByteArray body;
        bool ok = false;
        qint64 serializedBodyBytes = 0;
    };

    static Result serialize(const Input& input);
};
