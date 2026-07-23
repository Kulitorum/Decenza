#pragma once

#include <QSettings>

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
