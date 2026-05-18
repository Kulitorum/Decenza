import WidgetKit
import SwiftUI

struct MachineStatusEntry: TimelineEntry {
    let date: Date
    let display: WidgetDisplay
}

struct MachineStatusProvider: TimelineProvider {
    func placeholder(in context: Context) -> MachineStatusEntry {
        MachineStatusEntry(date: Date(),
                           display: WidgetDisplay.disconnectedState())
    }

    func getSnapshot(in context: Context,
                     completion: @escaping (MachineStatusEntry) -> Void) {
        completion(MachineStatusEntry(
            date: Date(),
            display: WidgetDisplay.from(MachineStatusSnapshot.load())))
    }

    func getTimeline(in context: Context,
                     completion: @escaping (Timeline<MachineStatusEntry>) -> Void) {
        let entry = MachineStatusEntry(
            date: Date(),
            display: WidgetDisplay.from(MachineStatusSnapshot.load()))
        // The app pushes WidgetCenter reloads on state changes while
        // connected; this fixed fallback only bounds staleness when the
        // app isn't running to push.
        let next = Calendar.current.date(byAdding: .minute, value: 15,
                                         to: Date()) ?? Date()
        completion(Timeline(entries: [entry], policy: .after(next)))
    }
}

struct DecenzaWidgetEntryView: View {
    @Environment(\.widgetFamily) private var family
    var entry: MachineStatusEntry

    // systemSmall is too tight for four rows + long phase names, so it shows
    // only the two glanceable essentials (phase + temp). Medium and larger
    // add the last-shot and staleness lines. Not user-configurable by design;
    // the content is right-sized to the family instead.
    private var compact: Bool { family == .systemSmall }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 6) {
                Image(systemName: "cup.and.saucer.fill")
                    .font(.system(size: 16))
                Text(entry.display.phaseLabel)
                    .font(.headline)
                    .lineLimit(2)
                    .minimumScaleFactor(0.8)
            }
            if !entry.display.tempLine.isEmpty {
                Text(entry.display.tempLine)
                    .font(.subheadline)
                    .lineLimit(1)
            }
            if !compact, !entry.display.lastShotLine.isEmpty {
                Text(entry.display.lastShotLine)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            Spacer(minLength: 0)
            // On compact, the only status worth the space is the
            // disconnected "Tap to open" prompt; staleness is dropped.
            if !entry.display.statusLine.isEmpty,
               !compact || entry.display.disconnected {
                Text(entry.display.statusLine)
                    .font(.caption2)
                    .foregroundStyle(.tertiary)
                    .lineLimit(1)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .containerBackground(.fill.tertiary, for: .widget)
    }
}

struct DecenzaWidget: Widget {
    let kind = "DecenzaMachineStatusWidget"

    var body: some WidgetConfiguration {
        StaticConfiguration(kind: kind, provider: MachineStatusProvider()) {
            entry in DecenzaWidgetEntryView(entry: entry)
        }
        .configurationDisplayName("Decenza")
        .description("DE1 machine status and last shot.")
        .supportedFamilies([.systemSmall, .systemMedium])
    }
}

@main
struct DecenzaWidgetBundle: WidgetBundle {
    var body: some Widget {
        DecenzaWidget()
    }
}
