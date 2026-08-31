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
// This is a known cross-platform trap, not a local discovery. Telegram Desktop
// shipped the same false positive (tdesktop#30511: a screen-reader banner on
// every Linux X11 desktop with no reader running, because the AT_SPI_BUS atom
// on the root window makes isActive() true), and resolved it the same way this
// does — platform-specific detection where a real answer exists, Qt's generic
// state elsewhere.
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
//          on macOS, so this is the whole set rather than one member of it.
//   iOS    UIAccessibilityIsVoiceOverRunning(), same reasoning.
//   Windows SPI_GETSCREENREADER — the flag NVDA, JAWS and Narrator all set, not a
//          per-product check, so a reader this code has never heard of counts too.
//   else   nullopt, and the caller keeps QAccessible::isActive().
//
// The two failure directions are not equally bad, which decides the design. Saying
// "a reader is present" when none is produces SILENCE — the whole feature dead,
// with a log line that reads like success. Saying "none is present" when one is
// produces DOUBLE SPEECH — irritating, but the user hears the content and can turn
// the app's own voice off. So where a platform check is imprecise it should err
// toward reporting a reader, which is what SPI_GETSCREENREADER does (Magnifier
// sets it without speaking).
std::optional<bool> decenzaPlatformScreenReaderActive();
