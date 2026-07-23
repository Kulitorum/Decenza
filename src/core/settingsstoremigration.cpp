#include "settingsstoremigration.h"

#include "appsettings.h"

#include <QDebug>
#include <QFile>

namespace {

constexpr auto kGuardKey = "migration/settings_store_de1qt_to_decenza_done";

// Clear the keys, then unlink the backing file.
//
// The unlink is best-effort by design, and on macOS it usually LOSES: cfprefsd
// owns the preference domain, and it writes its now-empty copy back to disk after
// we have removed the file — leaving a 0-key husk. That is why removeEmptyLegacyStoreFile()
// below runs on every launch rather than only during the migration: by the next
// launch the daemon has flushed and dropped the domain, and the unlink sticks.
// Deliberately not a retry loop or a delay — the next launch is the event, and
// this project does not use timers as guards.
//
// Correctness never depends on the unlink. The keys are gone either way; the file
// only decides whether the user still sees two preference files, which is the
// whole point of the change.
void destroyStore(QSettings& store, const char* what)
{
    const QString path = store.fileName();
    store.clear();
    store.sync();
    if (!path.isEmpty() && QFile::exists(path) && !QFile::remove(path))
        qInfo() << "Settings store migration: cleared" << what
                << "but its file lingers at" << path << "— will be removed on next launch";
}

#ifndef DECENZA_TESTING
// Idempotent: remove a legacy store's file if it exists and holds nothing. Cleans
// up the husk cfprefsd leaves behind (see above), and is a no-op on every launch
// thereafter. Only compiled into the shipped app — under DECENZA_TESTING its one
// caller is compiled out, and it would address a real store.
void removeEmptyLegacyStoreFile(const QString& organization, const QString& application)
{
    QSettings store(organization, application);
    store.setFallbacksEnabled(false);   // ask about THIS store, not the search list
    if (!store.allKeys().isEmpty())
        return;

    const QString path = store.fileName();
    if (!path.isEmpty() && QFile::exists(path) && QFile::remove(path))
        qInfo() << "Settings store migration: removed leftover empty store file at" << path;
}
#endif

}   // namespace

SettingsStoreMigrationOutcome migrateLegacySettingsStore(QSettings& canonical, QSettings& legacy)
{
    SettingsStoreMigrationOutcome out;

    // Both handles must answer about THEIR OWN store and nothing else.
    //
    // With fallbacks on — the Qt default — macOS merges the org-level and global
    // preference domains into allKeys() and contains(). The first real run of this
    // migration enumerated 118 "legacy" keys for a store that held 44: the other 74
    // were system preferences reached through the search list. They were skipped
    // only because contains() consulted the same fallbacks on the canonical side,
    // i.e. by symmetry rather than by design — break that symmetry and the
    // migration copies foreign keys into Decenza's store.
    canonical.setFallbacksEnabled(false);
    legacy.setFallbacksEnabled(false);

    if (canonical.value(QLatin1String(kGuardKey), false).toBool()) {
        out.alreadyDone = true;
        return out;
    }

    legacy.sync();
    if (legacy.status() != QSettings::NoError) {
        // Unreadable rather than absent — an absent store reports NoError with no
        // keys. Defer: stamping the guard here would abandon settings we cannot
        // currently see.
        out.deferredOnError = true;
        return out;
    }

    const QStringList legacyKeys = legacy.allKeys();
    out.legacyKeyCount = static_cast<int>(legacyKeys.size());

    QStringList copiedKeys;
    copiedKeys.reserve(legacyKeys.size());
    for (const QString& key : legacyKeys) {
        if (canonical.contains(key))
            continue;
        canonical.setValue(key, legacy.value(key));
        copiedKeys.append(key);
    }
    canonical.sync();
    if (canonical.status() != QSettings::NoError) {
        out.deferredOnError = true;
        return out;
    }

    // Read back every copied key before destroying the source. Both handles use
    // the same backend, so a value that survived the round trip compares equal;
    // anything else means the write did not land the way we asked.
    for (const QString& key : copiedKeys) {
        if (canonical.value(key) != legacy.value(key)) {
            qWarning() << "Settings store migration: verification failed for key" << key
                       << "— legacy store left intact, will retry on next launch";
            out.deferredOnError = true;
            return out;
        }
    }
    out.copied = static_cast<int>(copiedKeys.size());

    destroyStore(legacy, "legacy settings store");
    out.legacyDestroyed = true;

    canonical.setValue(QLatin1String(kGuardKey), true);
    canonical.sync();
    out.guardStamped = true;
    return out;
}

void runSettingsStoreMigrationOnce()
{
#ifdef DECENZA_TESTING
    // Never from a test process. Under DECENZA_TESTING the canonical handle is the
    // PID-scoped test file, but the legacy handle below is unconditionally the
    // developer's REAL DE1Qt store — and this migration destroys what it reads.
    // Tests exercise migrateLegacySettingsStore() directly, over temp stores.
    qWarning() << "Settings store migration: skipped (test build)";
    return;
#else
    // Store identities named only here — the branch that actually opens them.
    constexpr auto kLegacyOrganization = "DecentEspresso";
    constexpr auto kLegacyApplication = "DE1Qt";

    // The abandoned third store. AccessibilityManager wrote accessibility/* here
    // before it moved to the shared store; migrateAccessibilityLegacyStore()
    // carried those values over and stamps this guard in the shared store.
    constexpr auto kLegacyAccessibilityOrganization = "Decenza";
    constexpr auto kLegacyAccessibilityApplication = "DE1";
    constexpr auto kAccessibilityMigratedGuard = "accessibility/_migratedFromLegacyV1";

    // ORDERING: this must complete before Settings and AccessibilityManager are
    // constructed. AccessibilityManager's own legacy-store guard lives in the
    // store this migration populates — construct it first and it sees an
    // unstamped guard, re-runs its migration, and overwrites the user's current
    // accessibility settings with values from the abandoned ("Decenza", "DE1")
    // store.
    AppSettings canonical;
    QSettings legacy(QString::fromLatin1(kLegacyOrganization),
                     QString::fromLatin1(kLegacyApplication));

    const SettingsStoreMigrationOutcome r = migrateLegacySettingsStore(canonical, legacy);

    // Runs on every launch, including the already-migrated path: on macOS the
    // in-migration unlink loses a race with cfprefsd and leaves an empty husk,
    // and this is the launch that clears it.
    removeEmptyLegacyStoreFile(QString::fromLatin1(kLegacyOrganization),
                               QString::fromLatin1(kLegacyApplication));
    removeEmptyLegacyStoreFile(QString::fromLatin1(kLegacyAccessibilityOrganization),
                               QString::fromLatin1(kLegacyAccessibilityApplication));

    if (r.alreadyDone)
        return;
    if (r.deferredOnError) {
        qWarning() << "Settings store migration: deferred, legacy store left intact "
                      "and guard NOT stamped";
        return;
    }

    // qInfo (not qDebug) with the legacy key count, so a support log can tell
    // "nothing to migrate" (legacyKeyCount == 0) apart from "everything was
    // already present" (copied == 0 && legacyKeyCount > 0). An irreversible
    // one-time migration deserves a durable, unambiguous breadcrumb.
    qInfo() << "Settings store migration: copied" << r.copied << "of" << r.legacyKeyCount
            << "legacy key(s) into the canonical store; legacy store destroyed:"
            << r.legacyDestroyed;

    // The abandoned accessibility store is only safe to remove once its own
    // migration has stamped its guard — which the copy above may have just
    // carried across from the legacy store.
    if (canonical.value(QLatin1String(kAccessibilityMigratedGuard), false).toBool()) {
        QSettings legacyAccessibility(QString::fromLatin1(kLegacyAccessibilityOrganization),
                                      QString::fromLatin1(kLegacyAccessibilityApplication));
        if (!legacyAccessibility.allKeys().isEmpty())
            destroyStore(legacyAccessibility, "legacy accessibility store");
    }
#endif
}
