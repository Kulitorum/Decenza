#import <Foundation/Foundation.h>
#import <WidgetKit/WidgetKit.h>

#include <QByteArray>

#include "widgetsharedkeys.h"   // single source of truth for the keys

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
