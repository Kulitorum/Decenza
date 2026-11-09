#pragma once

#include <QString>
#include <QVersionNumber>

// The oldest data this build upgrades: what 2.0.6 and 2.0.7 shipped. Older
// data has to pass through kSteppingStoneRelease, which still carries the full
// migration chain. Every check and message below reads these.
namespace UpgradeFloor {

inline constexpr int kSchemaVersion = 41;
inline constexpr const char* kRelease = "2.0.7";
inline constexpr const char* kSteppingStoneRelease = "2.1.0";

// Refusal for a restore or transfer source below the floor. Callers that
// translate use kSourceTooOldKey with this text as the fallback.
inline constexpr const char* kSourceTooOldKey = "backup.error.sourceTooOld";
inline QString sourceTooOldMessage()
{
    return QStringLiteral("This data is from a Decenza release older than %1. Restore or transfer it "
                          "with Decenza %2 first.")
        .arg(QLatin1String(kRelease), QLatin1String(kSteppingStoneRelease));
}

// A transfer source's reported app version. Empty or unparseable reads as
// below the floor: every build since January 2026 reports one.
inline bool releaseSupported(const QString& appVersion)
{
    const QVersionNumber version = QVersionNumber::fromString(appVersion);
    return !version.isNull() && version >= QVersionNumber::fromString(QLatin1String(kRelease));
}

} // namespace UpgradeFloor
