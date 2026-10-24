#pragma once

#include "logtags.h"

#include <QString>
#include <QtGlobal>

#include <optional>

// Chains into qInstallMessageHandler to correct known Qt messages before they
// reach any other handler:
//   - drops "Missing CAP_NET_ADMIN permission..." bluetooth warning (Linux only)
//     when BleCapability::linuxMissing() is false — Qt prints it under
//     conditions that don't always mean caps are missing, misleading users
//     who have already granted the capability.
//   - drops "window doesn't exist." (all platforms) — QTBUG-146779, a harmless
//     critical-severity message Qt emits during startup/teardown.
//   - rewrites QML incremental-GC cross-validation misses (#2076) into a
//     [Memory][QmlGc] WARN, so a log scan finds them.
//
// Rewriting has to happen here rather than by logging a second line: Qt sends a
// message logged from inside a handler to stderr only (qlogging.cpp:2203-2210).
//
// Install AFTER AsyncLogger, CrashHandler, and WebDebugLogger so this
// filter sits at the top of the static chain.
namespace BtLogFilter {

void install();

/// Sets QV4_MM_CROSS_VALIDATE_INCREMENTAL_GC unless the environment already
/// sets it. Must run before the first QML/JS engine exists: Qt reads it once,
/// in the MemoryManager constructor (qv4mm.cpp:968).
void enableGcCrossValidation();

/// One miss marks its whole unreached subtree, so a single bug can print hundreds.
inline constexpr int kMaxGcMissLines = 20;

/// Qt prints each object the incremental mark missed as an unattributed qDebug
/// starting with this text (qv4mm.cpp:800).
inline bool isGcCrossValidationMessage(const QString& qtMessage)
{
    return qtMessage.startsWith(QLatin1String("Cross Validation Error on chunk"));
}

/// The WARN line for the occurrence'th (1-based) cross-validation message of this
/// run, or nullopt for a message that is not one, or one past the cap.
inline std::optional<QString> gcCrossValidationLine(const QString& qtMessage, int occurrence)
{
    if (!isGcCrossValidationMessage(qtMessage) || occurrence > kMaxGcMissLines + 1)
        return std::nullopt;
    // The full re-mark keeps the object alive, so the crash this check exists to
    // catch does not follow; this line is the only evidence.
    const QString text = occurrence > kMaxGcMissLines
        ? QStringLiteral("More incremental-GC misses this run; not logged after %1").arg(kMaxGcMissLines)
        : QStringLiteral("Incremental GC missed a live object; cross-validation kept it alive "
                         "(Qt bug behind #2076, please report): %1").arg(qtMessage);
    return DecenzaLog::prefix(QLatin1String(DECENZA_LOG_MARKER_MEMORY), QStringLiteral("QmlGc"))
         + QLatin1Char(' ') + text;
}

} // namespace BtLogFilter
