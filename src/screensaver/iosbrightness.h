#pragma once

// C-linkage helpers for iOS screen brightness control.
// Implemented in iosbrightness.mm (Objective-C++).
// Saved brightness is persisted to NSUserDefaults so it can be
// restored on next launch if the app crashes while dimmed.

#ifdef __cplusplus
extern "C" {
#endif

void ios_setScreenBrightness(float brightness);   // 0.0–1.0
void ios_restoreScreenBrightness();                // Restore to pre-dim value
void ios_checkAndRestoreBrightness();              // Call at startup to recover from crash while dimmed
void ios_setIdleTimerDisabled(bool disabled);      // Prevent iOS auto-lock (equivalent of FLAG_KEEP_SCREEN_ON)
void ios_setStatusBarStyle(bool isDarkTheme);      // Set status bar icons light (dark theme) or dark (light theme)

#if defined(__APPLE__)
void macos_probeEmojiFont();                        // Log which characters CoreText routes to Apple Color Emoji
#else
static inline void macos_probeEmojiFont() {}
#endif

#ifdef __cplusplus
}
#endif
