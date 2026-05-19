#pragma once

// Pure, Qt-free decision logic for the connection-priority EPOCH gate
// (scale-priority-epoch-scope-and-stall-confirm). BLEManager::setSettings()
// forwards the persisted record's latched flag + stored detectionEpoch and
// the compile-time kBleDetectionEpoch here, then performs the side effects
// (rehydrate / re-persist / clear + log) for the returned decision.
//
// Header-only and dependency-free ON PURPOSE: the full rehydrate / migrate /
// discard trichotomy — including the "legacy record migrates forward with
// ZERO extra detection" backward-compat guarantee — is then unit-testable
// without linking the heavy BLEManager translation unit (mirrors the
// BlePriorityDetector pure-logic pattern).

enum class BleEpochDecision {
    NoRecord,        // not latched — nothing to rehydrate
    Rehydrate,       // stored epoch == current → load the latch as-is
    MigrateForward,  // legacy pre-epoch record (epoch key absent) → honor + stamp current
    Discard          // a DIFFERENT epoch (deliberate bump) OR a corrupt value → wipe + re-detect
};

// `storedEpoch` is SettingsHardware::cpEpoch(): the persisted detectionEpoch,
// or -1 when the key is absent (a genuine pre-epoch / legacy record).
//
// Only EXACTLY -1 means "legacy". Any OTHER negative is corrupt persisted
// input (partial/truncated write, manual edit, format drift) and is treated
// as Discard → re-detect, NOT silently honored as a clean legacy latch (which
// would suppress the very re-detection the safety design intends and log a
// reassuring-but-false "migrated legacy forward"). `kBleDetectionEpoch` is a
// hand-assigned positive constant (never 0/negative, never versionCode), so
// the partition below is total and unambiguous for every int.
inline BleEpochDecision decideBleEpochGate(bool latched, int storedEpoch,
                                           int currentEpoch) {
    if (!latched) return BleEpochDecision::NoRecord;
    if (storedEpoch == currentEpoch) return BleEpochDecision::Rehydrate;
    if (storedEpoch == -1) return BleEpochDecision::MigrateForward;
    return BleEpochDecision::Discard;  // other epoch (bump) OR corrupt (incl. <0, !=-1)
}
