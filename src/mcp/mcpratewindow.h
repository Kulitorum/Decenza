#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>

// A per-caller, per-minute event counter.
//
// Two things in this subsystem need one and they were written separately:
// McpRemoteAccess counts failed token attempts per source address, and the
// modern (stateless) MCP era counts control-category tool calls per peer.
// The mechanism is identical — a key, a sliding 60-second window, a count — and
// only the budget differs, so it lives here once rather than being hand-rolled
// at each site.
//
// KEYED, never global: one caller must not be able to exhaust another's
// allowance. That is the failure the legacy per-session counter was shaped to
// avoid, and a stateless request has no session to count against.
class McpRateWindow {
public:
    // Records one event against `key` and reports whether that puts it OVER
    // `maxPerMinute`. Counts before deciding, so a failed or refused call still
    // consumes budget — otherwise a caller could spin on failures for free.
    bool recordAndCheckOverLimit(const QString& key, int maxPerMinute)
    {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        pruneStaleWindows(now);

        Window& window = m_windows[key];
        if (!window.start.isValid() || window.start.secsTo(now) >= WindowSeconds) {
            window.start = now;
            window.count = 0;
            window.suppressionLogged = false;
        }
        window.count++;
        return window.count > maxPerMinute;
    }

    // True exactly ONCE per window, for the caller that wants a single "further
    // requests are being dropped" line rather than one per refused request.
    // Going silent instead would leave a submitted log with no record that a
    // caller was being throttled at all.
    bool takeSuppressionLogSlot(const QString& key)
    {
        auto it = m_windows.find(key);
        if (it == m_windows.end() || it->suppressionLogged)
            return false;
        it->suppressionLogged = true;
        return true;
    }

    // Events recorded against `key` in the current window, 0 if none is live.
    // For a caller that logs by count rather than only by over/under — the
    // running total is what tells a submitted log a stray probe from a scanner
    // at thousands a minute, and going over the budget alone does not carry it.
    //
    // Read it straight after recordAndCheckOverLimit(), which prunes and rolls
    // the window first. On its own it reports whatever count is still resident,
    // which for an expired window is the old one — it does no pruning of its own
    // because a const accessor that mutates the map would be the worse surprise.
    int countInWindow(const QString& key) const
    {
        const auto it = m_windows.constFind(key);
        return it == m_windows.constEnd() ? 0 : it->count;
    }

    // Forget every key. For a caller tearing down the surface the budget
    // applies to — a fresh listener owes nobody a carried-over count.
    void clear() { m_windows.clear(); }

    // Drop windows that can no longer affect any decision, WITHOUT recording an
    // event. Call it from whatever periodic tick the owner already has.
    //
    // This exists because pruning on record alone is not enough, and the comment
    // that replaced McpRemoteAccess's reaper loop overstated it: records prune,
    // but after traffic STOPS nothing calls record, so whatever keys were live
    // at the last event stay resident for the process lifetime. The old reaper
    // ran regardless of traffic and emptied the map within a window of the last
    // attempt. That property was lost in the migration and is restored here.
    void pruneNow() { pruneStaleWindows(QDateTime::currentDateTimeUtc()); }

    // How many keys are currently tracked. Asserted by the test that pins
    // pruneNow(), so it is a real observation rather than the unused "test seam"
    // it was first introduced as.
    int trackedKeyCount() const { return static_cast<int>(m_windows.size()); }

private:
    struct Window {
        int count = 0;
        bool suppressionLogged = false;
        QDateTime start;
    };

    // Drop windows that can no longer affect any decision.
    //
    // The hash is keyed on a caller-supplied address, so without this it grows
    // for the process lifetime — one entry per distinct peer ever seen. Bounded
    // rather than capped: an entry two windows old is not evidence of anything,
    // since a new event would reset it anyway, so forgetting it changes no
    // outcome. The scan is over live keys only, which is small.
    void pruneStaleWindows(const QDateTime& now)
    {
        for (auto it = m_windows.begin(); it != m_windows.end();) {
            if (it->start.isValid() && it->start.secsTo(now) >= WindowSeconds * 2)
                it = m_windows.erase(it);
            else
                ++it;
        }
    }

    static constexpr int WindowSeconds = 60;
    QHash<QString, Window> m_windows;
};
