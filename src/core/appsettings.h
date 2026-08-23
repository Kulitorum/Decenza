#pragma once

#include <QSettings>
#include <QDebug>
#include <QString>

// The one handle onto Decenza's settings store.
//
// Every settings read and write in the app goes through this type. Nothing else
// constructs a QSettings — not the explicit organization/application form, not the
// bare default-constructed form. Two things depend on that being universal:
//
//   * There is exactly one store. The app used to write two (an explicit
//     ("DecentEspresso", "DE1Qt") handle in the Settings domain objects, and a
//     default-constructed handle everywhere else, which resolved through
//     QCoreApplication's org/app identity to a *different* store). Users saw two
//     preference files per platform, one named after an app Decenza has never
//     shipped as, and new code landed in whichever store its author happened to
//     construct. A third store, ("Decenza", "DE1"), was created the same way and
//     had to be migrated out of.
//
//   * Tests cannot touch a developer's real preferences. The DECENZA_TESTING
//     branch below redirects to a PID-scoped temp file. When call sites picked
//     their own construction, roughly half of them bypassed that redirect.
//
// Deliberately a subclass rather than an `appSettings()` factory: QSettings is a
// QObject and therefore non-copyable, so a factory cannot return one by value, and
// returning a reference to a shared instance would race the background-thread reads
// in ShotHistoryStorage and CoffeeBagStorage. A subclass keeps the existing
// one-handle-per-use pattern — `AppSettings settings;` is a one-token change from
// `QSettings settings;` — while naming the store identity in exactly one place.
class AppSettings : public QSettings
{
public:
    AppSettings();
};

// Stamp a one-time-migration flag, but only if the write actually reached the
// store. Returns false — leaving the flag unset so the migration retries next
// launch — when it is known to have failed.
//
// QSettings::status() is a sticky first-error latch: setStatus assigns only when
// the incoming status is NoError or the stored one already is
// (qtbase/src/corelib/io/qsettings.cpp:312-316), and none of its eleven call
// sites passes NoError. So a bare `status() != NoError` test on a store that
// latched an error earlier reports failure forever — re-running the migration's
// destructive work on every launch while never stamping. Only clean-before to
// dirty-after says anything about THIS write; when the store was already dirty
// there is no signal either way, so stamp optimistically.
inline bool commitMigrationFlag(QSettings& settings, const QString& flagKey,
                                const QString& what)
{
    const bool cleanBefore = (settings.status() == QSettings::NoError);
    settings.sync();
    if (cleanBefore && settings.status() != QSettings::NoError) {
        qWarning() << what << "may not have persisted (QSettings status"
                   << settings.status() << ") — leaving" << flagKey
                   << "unset so it retries next launch";
        return false;
    }
    settings.setValue(flagKey, true);
    return true;
}
