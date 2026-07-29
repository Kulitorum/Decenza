#pragma once

#include <QHash>
#include <QString>

// Collapses a repeating log line to one entry per window, carrying the count of what it stood in
// for.
//
// WHY THIS EXISTS AS ONE CLASS
// ----------------------------
// Three unrelated periodic sources — the MMR USB-charger keepalive (60 s), the memory sampler
// (60 s) and the ShotServer request log (a 5 s browser poll) — each wanted the same behaviour, and
// each was about to grow its own copy of it. In a 48-hour tablet capture they were 3,147 + 3,107 +
// 954 lines, 35% of the entire log, and every line was near-identical to the one before it. A
// fourth copy of "remember the last text, count repeats, print a summary" is exactly the drift the
// centralize rule in CLAUDE.md is about, so it lives here and they call it.
//
// It is deliberately NOT a general log filter and NOT tied to a category: callers keep their own
// qDebug()/qWarning() and their own wording. All this decides is whether to speak.
//
// NO TIMERS
// ---------
// The window is checked against the message's own arrival, not driven by a QTimer — a suppressed
// run is flushed by the next message in that run, not by a clock. That keeps this a pure decision
// function with no ownership, no thread affinity and nothing to stop, and it is why a source that
// falls silent mid-window simply stops: its last suppressed count is folded into the next line it
// does emit. For a periodic source (which is all three callers) the difference is one line at the
// end of the run, and in exchange there is no timer to leak or fire after teardown.
class LogCollapse
{
public:
    // `windowMs` is the minimum spacing between emitted lines for a given key while the text is
    // unchanged. A CHANGED text always emits immediately regardless of the window — a value that
    // moved is the interesting event, and holding it back is how a collapser turns into a bug.
    explicit LogCollapse(qint64 windowMs) : m_windowMs(windowMs) {}

    // Returns true when the caller should log, and sets `suppressed` to how many identical lines
    // were swallowed since the last emit (0 on the first line, or when the text changed).
    //
    // `nowMs` is passed in rather than read from a clock so the caller can reuse a timestamp it
    // already has, and so this is testable without waiting.
    bool shouldLog(const QString& key, const QString& text, qint64 nowMs, int* suppressed)
    {
        Entry& e = m_entries[key];
        const bool changed = (e.text != text);
        const bool windowElapsed = (nowMs - e.lastEmitMs) >= m_windowMs;

        if (!e.everEmitted || changed || windowElapsed) {
            if (suppressed)
                *suppressed = e.suppressed;
            e.text = text;
            e.lastEmitMs = nowMs;
            e.suppressed = 0;
            e.everEmitted = true;
            return true;
        }

        e.suppressed++;
        if (suppressed)
            *suppressed = 0;
        return false;
    }

    // Convenience for the common shape: " (+N identical in the last M s)" or an empty string.
    // Centralized so the three callers cannot word the same annotation three ways — which is what
    // happened to the [USB Scale] prefix (73 hand-written sites, 21 of them drifted).
    static QString suffix(int suppressed, qint64 windowMs)
    {
        if (suppressed <= 0)
            return QString();
        return QStringLiteral(" (+%1 identical in the last %2 s)")
            .arg(suppressed)
            .arg(windowMs / 1000);
    }

    QString suffix(int suppressed) const { return suffix(suppressed, m_windowMs); }

private:
    struct Entry
    {
        QString text;
        qint64 lastEmitMs = 0;
        int suppressed = 0;
        bool everEmitted = false;
    };

    qint64 m_windowMs;
    QHash<QString, Entry> m_entries;
};
