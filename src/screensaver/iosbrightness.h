#pragma once

// C-linkage helpers for iOS screen brightness control.
// Implemented in iosbrightness.mm (Objective-C).

#ifdef __cplusplus
extern "C" {
#endif

void ios_setScreenBrightness(float brightness);   // 0.0–1.0
void ios_restoreScreenBrightness();                // Restore to pre-dim value

#ifdef __cplusplus
}
#endif
