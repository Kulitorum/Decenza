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

void ios_setStatusBarStyle(bool isDarkTheme)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        UIWindow *window = nil;
        for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
            if ([scene isKindOfClass:[UIWindowScene class]]) {
                UIWindowScene *ws = (UIWindowScene *)scene;
                window = ws.windows.firstObject;
                break;
            }
        }
        if (window) {
            window.overrideUserInterfaceStyle = isDarkTheme ? UIUserInterfaceStyleDark : UIUserInterfaceStyleLight;
            qDebug() << "[Theme] iOS: set status bar style, isDark =" << isDarkTheme;
        }
    });
}

#else
// macOS — no UIScreen API, brightness control not available
void ios_setScreenBrightness(float) {}
void ios_restoreScreenBrightness() {}
void ios_checkAndRestoreBrightness() {}
void ios_setIdleTimerDisabled(bool) {}
void ios_setStatusBarStyle(bool) {}
#endif

// --- Font probe (macOS + iOS) — detect Apple Color Emoji fallback for non-emoji chars ---
#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>
#include <QDebug>
#include <QString>

void macos_probeEmojiFont()
{
    // Characters that appear in the Decenza UI — none should use Apple Color Emoji
    struct { UniChar ch; const char* name; } probes[] = {
        { 0x00B0, "DEGREE SIGN" },         // °
        { 0x00B7, "MIDDLE DOT" },           // ·
        { 0x2192, "RIGHTWARDS ARROW" },     // →
        { 0x2193, "DOWNWARDS ARROW" },      // ↓
        { 0x2199, "SW ARROW" },             // ↙
        { 0x0025, "PERCENT" },              // %
        { 0x0043, "LATIN C" },              // C
        { 0x2022, "BULLET" },               // •
        { 0x2713, "CHECK MARK" },           // ✓
        { 0x2714, "HEAVY CHECK MARK" },     // ✔
        { 0x2600, "SUN" },                  // ☀
        { 0x2601, "CLOUD" },                // ☁
        { 0x26A1, "HIGH VOLTAGE" },         // ⚡
        { 0x2744, "SNOWFLAKE" },            // ❄
    };

    CTFontRef systemFont = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, 16.0, NULL);
    if (!systemFont) {
        qWarning() << "[FontProbe] Failed to create system font";
        return;
    }

    bool anyEmoji = false;
    for (const auto& p : probes) {
        UniChar chars[1] = { p.ch };
        CTFontRef actualFont = CTFontCreateForString(systemFont,
            (__bridge CFStringRef)[NSString stringWithCharacters:chars length:1],
            CFRangeMake(0, 1));

        if (actualFont) {
            CFStringRef fontName = CTFontCopyPostScriptName(actualFont);
            NSString *name = (__bridge NSString *)fontName;

            // Check if it's Apple Color Emoji
            bool isEmoji = [name containsString:@"Emoji"] || [name containsString:@"emoji"];
            if (isEmoji) {
                qWarning() << "[FontProbe] WARNING: U+" << QString::number(p.ch, 16).toUpper()
                           << p.name << "-> font:" << QString::fromNSString(name)
                           << "(APPLE COLOR EMOJI — will trigger native rendering!)";
                anyEmoji = true;
            }

            if (fontName) CFRelease(fontName);
            CFRelease(actualFont);
        }
    }

    if (!anyEmoji) {
        qDebug() << "[FontProbe] All probed characters use non-emoji fonts (OK)";
    } else {
        qWarning() << "[FontProbe] Some characters use Apple Color Emoji!"
                    << "CurveTextRendering should still prevent the CopyEmojiImage crash.";
    }

    CFRelease(systemFont);
}
#endif
