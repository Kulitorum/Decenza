#include "core/screenreaderprobe.h"

#include <TargetConditionals.h>

#if TARGET_OS_IOS
#import <UIKit/UIAccessibility.h>
#else
// isVoiceOverEnabled is declared on NSWorkspace by AppKit/NSAccessibility.h, not by
// NSWorkspace.h — importing only the latter compiles to an unknown-selector error.
#import <AppKit/NSWorkspace.h>
#import <AppKit/NSAccessibility.h>
#endif

// Apple gives a direct answer to "is VoiceOver running", so use it rather than
// inferring one from whether accessibility is active.
//
// macOS: -[NSWorkspace isVoiceOverEnabled] (10.13+). It tracks VoiceOver
// specifically, not the AX bridge, which is the whole point — the bridge is
// what QAccessible::isActive() already reports and what misled the caller.
//
// iOS: UIAccessibilityIsVoiceOverRunning(). Included for symmetry and because
// the same reasoning holds, but note it is UNVERIFIED on device — this branch
// is compiled only by the iOS workflow and was not exercised when written. If
// an iOS accessibility report contradicts it, suspect this first.
//
// Neither call requires the app to be trusted for accessibility control, so
// there is no permission prompt and no failure mode where we get a wrong answer
// because a grant is missing.
std::optional<bool> decenzaPlatformScreenReaderActive()
{
#if TARGET_OS_IOS
    return UIAccessibilityIsVoiceOverRunning() ? true : false;
#else
    if (@available(macOS 10.13, *))
        return [[NSWorkspace sharedWorkspace] isVoiceOverEnabled] ? true : false;
    return std::nullopt;
#endif
}
