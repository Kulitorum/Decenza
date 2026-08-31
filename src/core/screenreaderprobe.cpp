#include "core/screenreaderprobe.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#endif

// Non-Apple platforms. Apple's answer lives in screenreaderprobe.mm.
std::optional<bool> decenzaPlatformScreenReaderActive()
{
#ifdef Q_OS_WIN
    // POSITIVE ONLY. A true from SPI_GETSCREENREADER means a reader announced
    // itself; a false means NOTHING, because Narrator does not set the flag at
    // all — Microsoft says so under both SPI_GETSCREENREADER and
    // SPI_SETSCREENREADER: "Narrator, the screen reader that is included with
    // Windows, does not set the SPI_SETSCREENREADER or SPI_GETSCREENREADER
    // flags."
    //
    // Treating that false as "no reader" is a real regression and was written
    // here once: the call SUCCEEDS and writes FALSE, so an engaged optional{false}
    // comes back, isScreenReaderActive() never consults QAccessible::isActive(),
    // and the app speaks over Narrator — the overlap #889 removed, aimed at the
    // default reader on Windows.
    //
    // Returning nullopt instead cannot regress anything: nullopt is main's
    // behaviour. NVDA and JAWS set the flag and are unaffected.
    BOOL screenReader = FALSE;
    if (SystemParametersInfo(SPI_GETSCREENREADER, 0, &screenReader, 0) && screenReader)
        return true;
    return std::nullopt;
#else
    // Android and Linux. nullopt means "no better answer than Qt's", so the
    // caller keeps QAccessible::isActive() — correct on Android, where TalkBack
    // is what activates accessibility, and known-wrong on Linux X11 for the
    // reason recorded in the header.
    return std::nullopt;
#endif
}
