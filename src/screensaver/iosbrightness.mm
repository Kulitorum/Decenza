#include "iosbrightness.h"

#if defined(Q_OS_IOS) || defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS

#import <UIKit/UIKit.h>

// Saved brightness before dimming, so we can restore it on wake
static CGFloat s_savedBrightness = -1.0;

void ios_setScreenBrightness(float brightness)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        // Save the original brightness on first dim call
        if (s_savedBrightness < 0) {
            s_savedBrightness = [UIScreen mainScreen].brightness;
        }
        [UIScreen mainScreen].brightness = brightness;
    });
}

void ios_restoreScreenBrightness()
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (s_savedBrightness >= 0) {
            [UIScreen mainScreen].brightness = s_savedBrightness;
            s_savedBrightness = -1.0;
        }
    });
}

#else
// macOS — no UIScreen, stubs only
void ios_setScreenBrightness(float) {}
void ios_restoreScreenBrightness() {}
#endif
#endif
