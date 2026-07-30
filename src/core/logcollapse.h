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

    // What a single emitted line stood in for. Both fields are needed to describe it honestly, and
    // they travel together for exactly that reason — see suffix().
    struct Collapsed
    {
        int suppressed = 0;   // identical lines swallowed since the last emit
        qint64 spanMs = 0;    // wall time those lines actually covered
    };

    // Returns true when the caller should log, and fills `out` with what the emitted line stands in
    // for (0/0 on the first line, or when the text changed).
    //
    // `nowMs` is passed in rather than read from a clock so the caller can reuse a timestamp it
    // already has, and so this is testable without waiting.
    bool shouldLog(const QString& key, const QString& text, qint64 nowMs, Collapsed* out)
    {
        Entry& e = m_entries[key];
        const bool changed = (e.text != text);
        const bool windowElapsed = (nowMs - e.lastEmitMs) >= m_windowMs;

        if (!e.everEmitted || changed || windowElapsed) {
            if (out)
                *out = {e.suppressed, e.everEmitted ? nowMs - e.lastEmitMs : 0};
            e.text = text;
            e.lastEmitMs = nowMs;
            e.suppressed = 0;
            e.everEmitted = true;
            return true;
        }

        e.suppressed++;
        if (out)
            *out = {};
        return false;
    }

    // Ends a run: returns what is still pending for `key` and forgets the key entirely, so the next
    // run starts as a first sighting.
    //
    // For a periodic source there is no run end and this is never needed. For an EPISODIC one there
    // is, and without this the tally is not merely late — it is misattributed. A broker outage that
    // recovers stops calling shouldLog(), so its suppressed count sits in the table until the NEXT
    // outage prints it, stapling last week's repeat count onto today's first failure. The caller
    // that observes the recovery is the only code that knows the run ended, so it is the only place
    // that can say so.
    Collapsed flush(const QString& key, qint64 nowMs)
    {
        const auto it = m_entries.constFind(key);
        if (it == m_entries.cend())
            return {};
        const Collapsed c{it->suppressed, it->everEmitted ? nowMs - it->lastEmitMs : 0};
        m_entries.erase(it);
        return c;
    }

    // Convenience for the common shape: " (+N identical in the preceding M s)" or an empty string.
    // Centralized so the five callers cannot word the same annotation five ways — which is what
    // happened to the [USB Scale] prefix (73 hand-written sites, 21 of them drifted).
    //
    // M IS THE MEASURED SPAN, NOT THE WINDOW. This took the window at first, and that was a lie
    // whenever the source was bursty rather than periodic. The window is only a MINIMUM: a run that
    // repeated for two minutes and then went quiet for three hours gets flushed by the next line
    // three hours later, and "in the last 600 s" would date a stale burst to the moment a reader is
    // looking at. For the periodic callers the two numbers are near-identical, which is precisely
    // why the error would have survived review — every log they produce looks right.
    static QString suffix(const Collapsed& c)
    {
        if (c.suppressed <= 0)
            return QString();
        return QStringLiteral(" (+%1 identical in the preceding %2 s)")
            .arg(c.suppressed)
            .arg(c.spanMs / 1000);
    }

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
