#pragma once

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "../core/steamgraphvalidation.h"

class RemoteSteamGraphSnapshot final
{
public:
    static constexpr qsizetype MaximumSamples = SteamGraphValidation::MaximumSamples;
    static constexpr qsizetype MaximumGoalPoints = SteamGraphValidation::MaximumGoalPoints;
    static constexpr qsizetype MaximumResponseBytes = 2 * 1024 * 1024;

    struct Snapshot {
        QString packageId;
        QString hostId;
        qint64 revision = -1;
        double rawTime = 0.0;
        QVector<QPointF> pressure;
        QVector<QPointF> flow;
        QVector<QPointF> temperature;
        QVector<QPointF> flowGoalPoints;
    };

    struct Result {
        int httpStatus = 200;
        QByteArray body;
        bool ok = false;
    };

    // One schema is shared by the host serializer and browser parser. Curves are
    // complete ordered [time,value] sequences; oversized traces fail atomically.
    static Result serialize(const Snapshot& snapshot);
    static bool parse(const QByteArray& body, const QString& expectedPackageId,
                      const QString& expectedHostId, qint64 minimumRevision,
                      Snapshot* snapshot, QString* error);
};
