#import <Foundation/Foundation.h>
// WidgetCenter is a Swift-first API: it is NOT in the WidgetKit umbrella
// header, so #import <WidgetKit/WidgetKit.h> leaves it undeclared in
// Obj-C++. The Clang module import exposes the generated Obj-C interface
// (Xcode app targets enable modules by default).
@import WidgetKit;

#include <QByteArray>

#include "widget/widgetsharedkeys.h"   // single source of truth for the keys
                                       // (src/ is the target's header root)

// Writes the machine-status snapshot to the App Group shared UserDefaults so
// the WidgetKit extension can read it, then asks WidgetKit to refresh the
// timeline. reloadAllTimelines is a harmless no-op when no widget is installed.
void decenzaWriteWidgetSnapshotIOS(const QByteArray& json) {
    @autoreleasepool {
        NSString* appGroup =
            @(WidgetSharedKeys::kIosAppGroupId);
        NSString* key = @(WidgetSharedKeys::kIosUserDefaultsKey);

        NSUserDefaults* shared =
            [[NSUserDefaults alloc] initWithSuiteName:appGroup];
        if (!shared) {
            // The single most likely real-world misconfig for this feature
            // (wrong/disabled App Group entitlement). Make it grep-able
            // instead of an invisible "widget stuck on Disconnected".
            NSLog(@"[widget] App Group '%@' UserDefaults unavailable — "
                   "entitlement/group-id misconfig?", appGroup);
            return;
        }

        NSString* value =
            [[NSString alloc] initWithBytes:json.constData()
                                     length:(NSUInteger)json.size()
                                   encoding:NSUTF8StringEncoding];
        if (!value) {
            NSLog(@"[widget] snapshot UTF-8 decode failed (%lld bytes)",
                  (long long)json.size());
            return;
        }

        [shared setObject:value forKey:key];

        if (@available(iOS 14.0, *)) {
            [WidgetCenter.sharedCenter reloadAllTimelines];
        }
    }
}
