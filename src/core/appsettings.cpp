#include "appsettings.h"

#ifdef DECENZA_TESTING

#include "settings.h"   // Settings::testQSettingsPath()

AppSettings::AppSettings()
    : QSettings(Settings::testQSettingsPath(), QSettings::IniFormat)
{
}

#else

namespace {

// The canonical store identity. Named here and nowhere else.
//
// The explicit two-argument form is used rather than the default constructor
// because the default form resolves against QCoreApplication state that main.cpp
// temporarily reassigns while migrating the old "Decenza DE1" application name — a
// handle constructed inside that window would silently address a different store.
// tst_appsettings asserts the two forms otherwise agree on the same backing file.
constexpr auto kOrganization = "DecentEspresso";
constexpr auto kApplication = "Decenza";

}   // namespace

AppSettings::AppSettings()
    : QSettings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication))
{
}

#endif
