#include "core/screenreaderprobe.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#endif

// Non-Apple platforms. Apple's answer lives in screenreaderprobe.mm.
std::optional<bool> decenzaPlatformScreenReaderActive()
{
#ifdef Q_OS_WIN
    // SPI_GETSCREENREADER is the documented Windows flag a screen reader sets so
    // applications can adapt; NVDA, JAWS and Narrator all set it. It is what
    // Microsoft's own guidance points at, and it is strictly better here than
    // QAccessible::isActive(), which merely means a UI Automation client attached.
    //
    // It over-reports: Windows Magnifier sets it too, and Magnifier does not
    // speak. That direction is the safe one — over-reporting means the app stays
    // quiet and the reader (or nothing) speaks, while UNDER-reporting would mean
    // the app and a real screen reader both talk over the user, which is the
    // overlap #889 removed. It is still a worse outcome than being right, so if a
    // Windows user reports silence with only Magnifier running, this is the line.
    BOOL screenReader = FALSE;
    if (SystemParametersInfo(SPI_GETSCREENREADER, 0, &screenReader, 0))
        return screenReader ? true : false;
    return std::nullopt;
#else
    // Android and Linux. nullopt means "no better answer than Qt's", so the
    // caller keeps QAccessible::isActive() — correct on Android, where TalkBack
    // is what activates accessibility, and known-wrong on Linux X11 for the
    // reason recorded in the header.
    return std::nullopt;
#endif
}
