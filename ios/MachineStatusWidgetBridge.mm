#import <Foundation/Foundation.h>
#import <WidgetKit/WidgetKit.h>

#include <QByteArray>
#include <QString>

// Mirrors src/widget/widgetsharedkeys.h — keep in sync.
static NSString* const kAppGroupId = @"group.io.github.kulitorum.decenza";
static NSString* const kSnapshotKey = @"machineStatusSnapshot";

// Writes the machine-status snapshot to the App Group shared UserDefaults so
// the WidgetKit extension can read it, then asks WidgetKit to refresh the
// timeline. reloadAllTimelines is a harmless no-op when no widget is installed.
void decenzaWriteWidgetSnapshotIOS(const QByteArray& json) {
    @autoreleasepool {
        NSUserDefaults* shared =
            [[NSUserDefaults alloc] initWithSuiteName:kAppGroupId];
        if (!shared)
            return;

        NSString* value =
            [[NSString alloc] initWithBytes:json.constData()
                                     length:(NSUInteger)json.size()
                                   encoding:NSUTF8StringEncoding];
        if (!value)
            return;

        [shared setObject:value forKey:kSnapshotKey];

        if (@available(iOS 14.0, *)) {
            [WidgetCenter.sharedCenter reloadAllTimelines];
        }
    }
}
