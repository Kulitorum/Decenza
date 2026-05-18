#import <Foundation/Foundation.h>

#include <QByteArray>

#include "widget/widgetsharedkeys.h"   // single source of truth for the keys
                                       // (src/ is the target's header root)

// Writes the machine-status snapshot to the App Group shared UserDefaults so
// the WidgetKit extension can read it.
//
// NOTE: this intentionally does NOT call WidgetCenter.reloadAllTimelines().
// WidgetCenter is Swift-only — it is absent from the WidgetKit umbrella
// header, and the Qt iOS build compiles .mm with C++ modules disabled
// (`-x objective-c++`, no -fmodules) so `@import WidgetKit;` is rejected.
// There is therefore no Obj-C++ path to it in this build config. The iOS
// widget instead refreshes on its own WidgetKit timeline policy (see
// DecenzaWidget.swift getTimeline .after(...)), consistent with the
// design's "last-known state, not real-time" non-goal; the staleness line
// keeps it honest. Instant iOS push-refresh, if ever wanted, needs an
// @objc Swift shim compiled into the app target (follow-up).
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
    }
}
