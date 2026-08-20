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

    // Forget every key. For a caller tearing down the surface the budget
    // applies to — a fresh listener owes nobody a carried-over count.
    void clear() { m_windows.clear(); }

    // Test seam: how many keys are currently tracked.
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
