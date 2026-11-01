#pragma once

#include "core/diagnosticlogging.h"
#include <QElapsedTimer>
#include <array>

// GUI-thread counters: one bounded summary per active path, at most every 10 seconds.
namespace UiWorkStats {
enum class Kind {
    SteamPublish, SteamApply, BrowserBindings, WidgetFormat, SteamRefresh, SteamEditorRevision, Count
};
struct Counter {
    explicit Counter(const char* label) : name(label) {}
    const char* name;
    QElapsedTimer window;
    quint64 calls = 0;
    quint64 lifetimeCalls = 0;
    quint64 changes = 0;
    quint64 items = 0;
    qint64 totalNs = 0;
    qint64 maximumNs = 0;

    void record(qint64 elapsedNs, quint64 changed, quint64 workItems) {
        if (!window.isValid()) window.start();
        ++calls;
        ++lifetimeCalls;
        changes += changed;
        items += workItems;
        totalNs += elapsedNs;
        maximumNs = qMax(maximumNs, elapsedNs);
        const qint64 elapsedMs = window.elapsed();
        if (elapsedMs < 10000) return;
        DIAG_DEBUG(APP, "UiWorkStats") << name << "windowMs=" << elapsedMs
            << "calls=" << calls << "changes=" << changes << "items=" << items
            << "totalMs=" << double(totalNs) / 1e6
            << "maxMs=" << double(maximumNs) / 1e6;
        calls = changes = items = 0;
        totalNs = maximumNs = 0;
        window.restart();
    }
};
inline Counter& counter(Kind kind) {
    static std::array<Counter, static_cast<size_t>(Kind::Count)> counters{
        Counter("steam-publish"), Counter("steam-apply"), Counter("browser-bindings"),
        Counter("widget-format"), Counter("steam-refresh"), Counter("steam-editor-revision")};
    return counters[static_cast<size_t>(kind)];
}
struct Span {
    explicit Span(Kind kind) : sink(counter(kind)) { timer.start(); }
    ~Span() { sink.record(timer.nsecsElapsed(), changes, items); }
    Counter& sink;
    QElapsedTimer timer;
    quint64 changes = 0;
    quint64 items = 0;
};
}
