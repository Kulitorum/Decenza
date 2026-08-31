#pragma once

#include <optional>

// Is a screen reader actually going to SPEAK what we hand the platform?
//
// AccessibilityManager suppresses its own QTextToSpeech when it believes one is
// active, so the two do not talk over each other (#889). QAccessible::isActive()
// cannot answer this: Qt documents it as false "until a tool such as a screen
// reader accessed the accessibility framework", so it reports ATTACHMENT. An
// automation tool or an inspector flips it with no reader present, and the app
// then hands every announcement to nobody and stays silent itself. Qt reports
// nothing about whether an announcement was consumed, so this has to be decided
// before dispatch or not at all.
//
//   macOS   -[NSWorkspace isVoiceOverEnabled]. VoiceOver is the only screen
//           reader in practical use there — revisit if a macOS user reports the
//           app talking over some other one.
//   iOS     UIAccessibilityIsVoiceOverRunning().
//   Windows SPI_GETSCREENREADER, POSITIVE ONLY. Narrator does not set it
//           (Microsoft documents this), so a false means "unknown" and returns
//           nullopt — never "no reader". Treating it as "no reader" makes the app
//           speak over Narrator; see the .cpp.
//   else    nullopt, and the caller keeps QAccessible::isActive().
//
// Err toward nullopt when unsure. Reporting a reader that is not there kills the
// feature silently; reporting none when one is present merely doubles the speech,
// which the user can hear and switch off.
//
// Linux is the known-bad fallback: the AT_SPI_BUS atom makes isActive() true with
// no reader running (tdesktop#30511). Left alone deliberately — that fix belongs
// in Qt and is being made there (QTBUG-145881), though it is not in 6.11.2, whose
// dbusconnection.cpp still sets m_enabled unconditionally from the atom. A Qt
// bump will fix it here for free.
std::optional<bool> decenzaPlatformScreenReaderActive();

// Extracted so it can be tested: an engaged optional{false} short-circuits the
// fallback, so "the call answered no" and "nobody could say" must stay distinct.
// That distinction is where this file's one Windows bug lived.
inline bool decenzaResolveScreenReaderActive(std::optional<bool> platform, bool qtIsActive)
{
    return platform.value_or(qtIsActive);
}
