#include "iosbrightness.h"

#if defined(Q_OS_IOS) || defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS

#import <UIKit/UIKit.h>
#include <QDebug>

static NSString * const kSavedBrightnessKey = @"DecenzaSavedBrightness";

// Saved brightness before dimming, so we can restore it on wake
static CGFloat s_savedBrightness = -1.0;

void ios_setScreenBrightness(float brightness)
{
    float clamped = fminf(fmaxf(brightness, 0.0f), 1.0f);
    dispatch_async(dispatch_get_main_queue(), ^{
        // Save the original brightness on first dim call
        if (s_savedBrightness < 0) {
            s_savedBrightness = [UIScreen mainScreen].brightness;
            // Persist so we can restore if app crashes while dimmed
            [[NSUserDefaults standardUserDefaults] setObject:@(s_savedBrightness)
                                                      forKey:kSavedBrightnessKey];
            qDebug() << "[Screensaver] iOS: saved original brightness:" << s_savedBrightness;
        }
        [UIScreen mainScreen].brightness = clamped;
        qDebug() << "[Screensaver] iOS: set brightness to" << clamped;
    });
}

void ios_restoreScreenBrightness()
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (s_savedBrightness >= 0) {
            [UIScreen mainScreen].brightness = s_savedBrightness;
            qDebug() << "[Screensaver] iOS: restored brightness to" << s_savedBrightness;
            s_savedBrightness = -1.0;
        }
        [[NSUserDefaults standardUserDefaults] removeObjectForKey:kSavedBrightnessKey];
    });
}

void ios_checkAndRestoreBrightness()
{
    NSNumber *saved = [[NSUserDefaults standardUserDefaults] objectForKey:kSavedBrightnessKey];
    if (saved != nil) {
        float brightness = [saved floatValue];
        qDebug() << "[Screensaver] iOS: recovering brightness after crash:" << brightness;
        dispatch_async(dispatch_get_main_queue(), ^{
            [UIScreen mainScreen].brightness = brightness;
            // Clear persisted key only after brightness is actually restored
            [[NSUserDefaults standardUserDefaults] removeObjectForKey:kSavedBrightnessKey];
        });
    }
}

void ios_setIdleTimerDisabled(bool disabled)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIApplication sharedApplication].idleTimerDisabled = disabled ? YES : NO;
        qDebug() << "[Screensaver] iOS: idleTimerDisabled =" << disabled;
    });
}

#else
// macOS — no UIScreen API, brightness control not available
void ios_setScreenBrightness(float) {}
void ios_restoreScreenBrightness() {}
void ios_checkAndRestoreBrightness() {}
void ios_setIdleTimerDisabled(bool) {}
#endif
#endif
