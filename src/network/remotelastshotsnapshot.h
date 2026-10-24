#pragma once

#include "../history/shotprojection.h"
#include "lastshotsnapshotlimits.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace RemoteLastShotSnapshot {

using LastShotSnapshotLimits::MaximumEntriesPerSeries;
using LastShotSnapshotLimits::MaximumPhaseMarkers;
using LastShotSnapshotLimits::MaximumResponseBytes;

struct Result {
    int httpStatus = 200;
    QByteArray body;
};

// Serialize one coherent last-shot projection for the remote session API.
// Nothing is shortened: inputs outside the declared bounds fail explicitly.
Result serialize(const QString& packageId, const QString& hostId, quint64 revision,
                 const std::optional<ShotProjection>& shot);
Result error(int httpStatus, const QString& packageId, const QString& hostId,
             quint64 revision, const QString& code, const QString& detail);

} // namespace RemoteLastShotSnapshot
