#pragma once

#include "servicepayload.h"
#include <QVariantMap>

namespace HostSessionPayload {
// Splitting the wire properties must not give each half a separate payload budget.
constexpr qint64 MaximumBytes = 128 * 1024;

inline qint64 appearanceSize(const QVariantMap& appearance)
{
    return ServicePayload::serializedSize(appearance, MaximumBytes);
}

inline bool fitsTelemetry(const QVariantMap& telemetry, qint64 appearanceBytes)
{
    return appearanceBytes >= 0 && appearanceBytes <= MaximumBytes
        && ServicePayload::fits(telemetry, MaximumBytes - appearanceBytes);
}
}
