#pragma once

#include <QSettings>

// One-time migration of every key from the legacy ("DecentEspresso", "DE1Qt")
// store into the canonical store (see appsettings.h), after which the legacy
// store is destroyed.
//
// Decenza wrote two stores for years: this one, named after an app it has never
// shipped as, and the app-named store a default-constructed QSettings resolves
// to. Users saw both — two plists on macOS, two registry keys on Windows, two
// .conf files on Linux and Android — and new code landed in whichever one its
// author happened to construct.
struct SettingsStoreMigrationOutcome {
    int copied = 0;              // keys written into the canonical store
    int legacyKeyCount = 0;      // keys the legacy store held (0 == nothing to do)
    bool alreadyDone = false;    // guard was already stamped; nothing was touched
    bool deferredOnError = false;// aborted intact; guard NOT stamped, retry next launch
    bool guardStamped = false;
    bool legacyDestroyed = false;// legacy keys cleared (file removal is best-effort)
};

// Pure over the two handles it is given, so tests drive it with temp IniFormat
// stores and never touch a real one.
//
// Skip-if-present: a key already in the canonical store keeps its value, because
// that value is one the running build wrote. Every key it does copy is read back
// and compared before the legacy store is destroyed; on any mismatch — or if the
// legacy store cannot be read — it aborts without stamping the guard and without
// destroying anything, so the next launch retries. That defer-on-error contract
// is borrowed from migrateAccessibilityLegacyStore(), and matters more here
// because this migration deletes its own source.
SettingsStoreMigrationOutcome migrateLegacySettingsStore(QSettings& canonical, QSettings& legacy);

// Wrapper over the real stores, for main.cpp. Logs the outcome at qInfo.
// MUST run before Settings or AccessibilityManager are constructed — see the
// ordering note in the .cpp.
void runSettingsStoreMigrationOnce();
