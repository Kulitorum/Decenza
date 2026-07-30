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
#include "core/fontlogging.h"
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
        FONT_WARN("Probe", QStringLiteral("Failed to create system font"));
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
                FONT_LOG("Probe", QStringLiteral("U+%1 %2 would resolve to %3 IF rendered as "
                                                 "text — expected; the app renders it as a "
                                                 "bundled SVG")
                                      .arg(QString::number(p.ch, 16).toUpper(),
                                           QString::fromLatin1(p.name),
                                           QString::fromNSString(name)));
                anyEmoji = true;
            }

            if (fontName) CFRelease(fontName);
            CFRelease(actualFont);
        }
    }

    if (!anyEmoji) {
        FONT_LOG("Probe", QStringLiteral("All probed characters use non-emoji fonts (OK)"));
    } else {
        // This used to say "CurveTextRendering should still prevent the CopyEmojiImage crash."
        // That is FALSE, and a crash on 2026-07-18 disproved it: curves cannot represent colour
        // bitmaps, so Qt falls back to the texture-mask path for exactly these glyphs. Debug logs
        // are read by users' AIs via MCP and acted on, so a confidently wrong all-clear here would
        // get a real crash report dismissed. The actual defence is never letting a colour glyph
        // reach the renderer — Theme.replaceEmojiWithImg() rewrites emoji to bundled SVGs.
        //
        // DEBUG, not INFO, and the audience rule is why: every line of this is
        // addressed to whoever is investigating a suspected render crash, and to
        // nobody else. It also fires on EVERY Apple-platform startup — the probe
        // set includes ⚡ ☀ ☁ ❄, which Apple Color Emoji always covers — so at
        // INFO it was two paragraphs of "this is normal" standing permanently in
        // the user-facing narrative. Demoting costs the intended reader nothing:
        // debug_get_log returns DEBUG by default, so the AI reading a submitted
        // log still sees it exactly when it is looking here.
        FONT_LOG("Probe",
                 QStringLiteral("Probed characters have Apple Color Emoji coverage. That is "
                                "normal and NOT a crash indicator: the app renders emoji as "
                                "bundled SVG images, so these glyphs never reach the text "
                                "renderer. A crash here would mean some string bypassed "
                                "Theme.replaceEmojiWithImg() — that is the thing to look for, "
                                "not this line."));
    }

    CFRelease(systemFont);
}
#endif
