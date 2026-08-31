#pragma once

#include <optional>

// Is a screen reader actually going to SPEAK what we hand the platform?
//
// AccessibilityManager suppresses its own QTextToSpeech whenever it believes a
// screen reader is active, so the two do not talk over each other (#889). The
// belief was QAccessible::isActive(), and that answers a different question:
// "has an assistive client attached to this process". Anything touching the
// platform accessibility API flips it — an automation tool, an inspector, a
// remote-control agent — with no screen reader anywhere.
//
// When that happens the app hands every announcement to a reader that is not
// there and says nothing itself. Observed on macOS 2026-08-30: VoiceOver off,
// QAccessible::isActive() true, accessibility enabled, and the log full of
// `[Accessibility][Route] path=platform isActive=true` — which reads like
// success. Qt reports nothing about whether an announcement was consumed, so
// there is no delivery signal to fall back on; the question has to be answered
// before dispatch or not at all.
//
// Qt documents isActive() as returning false "until a tool such as a screen
// reader accessed the accessibility framework" — an ATTACHMENT test, which is
// what it is good for (skipping expensive updateAccessibility() calls) and not
// what routing needs.
//
// This is a known cross-platform trap, not a local discovery: Telegram Desktop
// shipped the same false positive (tdesktop#30511 — a screen-reader banner on
// every Linux X11 desktop with no reader running, because the AT_SPI_BUS atom on
// the root window makes isActive() true).
//
// Be careful what that precedent is cited FOR, because an earlier draft of this
// comment got it backwards. tdesktop did NOT settle on an app-side per-platform
// probe: a PR adding Linux detection via org.a11y.Status.ScreenReaderEnabled was
// argued down in review as the wrong layer, and the fix landed in Qt instead
// (QTBUG-145881, a QAtSpiDBusConnection constructor-order bug on XCB). So the
// issue is evidence that isActive() over-reports, and evidence that the LINUX
// half of it belongs upstream — not evidence for this file's approach. This file
// leaves Linux alone for exactly that reason. That Qt change is NOT in 6.11.2 —
// checked on Gerrit (still open, targeting dev) and against
// ~/Qt/6.11.2/Src/qtbase/src/gui/accessible/linux/dbusconnection.cpp, which
// still sets m_enabled unconditionally from the XCB atom. So Linux keeps the
// over-reporting today; when the Qt fix lands, a version bump fixes it here for
// free and no AT-SPI query needs writing.
//
// Returns nullopt where we have no better answer than Qt's, and the caller
// keeps using QAccessible::isActive(). On Android that is a good proxy: TalkBack
// is what activates accessibility and little else routinely does. On Linux X11
// it is NOT — the AT-SPI atom makes it wrong in exactly the way tdesktop hit,
// so a Linux user with no reader gets the same silence this fixes on macOS.
// Left unfixed deliberately: it needs an AT-SPI query nobody here can test
// today, and stating the gap is better than guessing at it.
// WHICH READERS, not just VoiceOver. Each platform is asked the broadest question
// it can actually answer:
//
//   macOS  -[NSWorkspace isVoiceOverEnabled]. VoiceOver is the only screen reader
//          in practical use on macOS, so this is close enough to the whole set to
//          act on — not a verified absolute, and it is the assumption to revisit
//          if a macOS user reports the app talking over some other reader.
//   iOS    UIAccessibilityIsVoiceOverRunning(), same reasoning.
//   Windows SPI_GETSCREENREADER, POSITIVE ONLY — the flag NVDA and JAWS set. It
//          is not a per-product check, so a reader this code has never heard of
//          counts too. Narrator does NOT set it (Microsoft documents that), so a
//          false from the call means "unknown" and returns nullopt, never "no
//          reader" — see the .cpp for the regression that distinction prevents.
//   else   nullopt, and the caller keeps QAccessible::isActive().
//
// The two failure directions are not equally bad, and an earlier draft of this
// comment got the conclusion backwards, so state it carefully:
//
//   Reporting a reader when there is none  -> the app stays quiet and nothing
//       speaks. SILENCE, the whole feature dead, with a log line that reads like
//       success. This is the bug that prompted the file.
//   Reporting no reader when there is one  -> the app speaks alongside the
//       reader. DOUBLE SPEECH: irritating, but the content is heard and the
//       app's own voice can be switched off.
//
// So silence is the worse outcome and an imprecise check should err toward
// reporting NO reader — i.e. toward nullopt, which hands the decision back to
// QAccessible::isActive() rather than deciding it wrongly. Windows Magnifier
// still sets SPI_GETSCREENREADER without speaking, so a Magnifier-only user gets
// the silent outcome; that is no worse than before this file existed, because
// isActive() is true for Magnifier too.
std::optional<bool> decenzaPlatformScreenReaderActive();

// How a probe answer and Qt's flag combine. Trivial, and extracted anyway,
// because the one Windows bug this file has had lived exactly here: an engaged
// optional{false} short-circuits the fallback, so "the call answered no" and
// "nobody could say" must not be the same value. Pinning the three cases in a
// test is what makes that difference visible to the next reader.
inline bool decenzaResolveScreenReaderActive(std::optional<bool> platform, bool qtIsActive)
{
    return platform.value_or(qtIsActive);
}
