#pragma once

#include <QtTypes>

// Shared protocol bounds for the native serializer and the Wasm client. Keep
// this header free of ShotProjection/database includes so the browser target
// can validate the same limits without importing native history machinery.
namespace LastShotSnapshotLimits {

constexpr qsizetype MaximumResponseBytes = 2 * 1024 * 1024;
constexpr qsizetype MaximumEntriesPerSeries = 6000;
constexpr qsizetype MaximumPhaseMarkers = 512;

} // namespace LastShotSnapshotLimits
