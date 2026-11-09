#pragma once

#include <QString>

// The oldest data this build upgrades, by shot database schema version. Older
// data has to pass through kSteppingStoneRelease, which still carries the full
// migration chain. Every check and message reads these.
namespace UpgradeFloor {

inline constexpr int kSchemaVersion = 41;
inline constexpr const char* kSteppingStoneRelease = "2.1.0";

// Refusal for a restore or transfer source below the floor. Callers that
// translate use kSourceTooOldKey with this text as the fallback.
inline constexpr const char* kSourceTooOldKey = "backup.error.sourceTooOld";
inline QString sourceTooOldMessage()
{
    return QStringLiteral("This data is from an older release of Decenza. Restore or transfer it "
                          "with Decenza %1 first.")
        .arg(QLatin1String(kSteppingStoneRelease));
}

} // namespace UpgradeFloor
