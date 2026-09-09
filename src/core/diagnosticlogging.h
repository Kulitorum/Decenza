#pragma once

#include "core/logtags.h"
#include <QDebug>

// Stream aliases for mixed-owner files. Owner is a registry identifier (BATTERY,
// SHOT, etc.), not a class name or a bracketed string. Existing subsystem helpers
// remain the API for their device signals and repeat-collapsed events.
#define DIAG_DEBUG(owner, tag) DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_##owner, tag, qDebug)
#define DIAG_INFO(owner, tag) DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_##owner, tag, qInfo)
#define DIAG_WARN(owner, tag) DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_##owner, tag, qWarning)
#define DIAG_ERROR(owner, tag) DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_##owner, tag, qCritical)
#define DIAG_FATAL(owner, tag) DECENZA_SUBSYS_STREAM(DECENZA_LOG_MARKER_##owner, tag, qFatal)

// Keep category enablement at the emitting site (qC* can compile to a loop, so
// these are statement streams; append values, not QDebug modifiers).
#define DIAG_CDEBUG(owner, tag, category) \
    qCDebug(category).noquote() << DecenzaLog::prefix(QLatin1String(DECENZA_LOG_MARKER_##owner), QLatin1String(tag)) // log-marker-exempt: shared category formatter preserves Qt category filtering
#define DIAG_CWARN(owner, tag, category) \
    qCWarning(category).noquote() << DecenzaLog::prefix(QLatin1String(DECENZA_LOG_MARKER_##owner), QLatin1String(tag)) // log-marker-exempt: shared category formatter preserves Qt category filtering
