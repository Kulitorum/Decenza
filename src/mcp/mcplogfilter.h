#pragma once

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

// Shared filter/tail helpers for the debug_get_log and shots_get_debug_log MCP
// tools, extracted so the pure-string-matching logic can be unit-tested
// without the persisted log file or a shot database (see mcptools_shots_helpers.h
// for the precedent).

namespace McpLogFilter {

// One line (or, after dedupeConsecutive(), one run of consecutive identical
// lines) addressed by its absolute position within whatever range the caller
// is searching (whole log, one session, or one shot's debug log). `count`/
// `lastLine` are only meaningful after dedupeConsecutive() — a plain
// filterLines() result always has count == 1 and lastLine == line.
struct LineMatch {
    qsizetype line = 0;      // absolute line number of the first occurrence
    QString text;            // text of the first occurrence
    qsizetype count = 1;     // consecutive occurrences collapsed into this entry
    // Absolute line of the last occurrence. Defaults to match `line` rather than to
    // a -1 sentinel: no code path ever leaves it at -1 (filterLines() sets it equal
    // to `line`, dedupeConsecutive() only widens it), and a sentinel that cannot
    // occur invites a check for it that can never fire.
    qsizetype lastLine = 0;
};

// DEBUG < INFO < WARN < ERROR < FATAL; -1 for anything else (session markers,
// trim banners — lines WebDebugLogger itself did not emit via handleMessage()).
inline int levelRank(const QString& level)
{
    static const QHash<QString, int> ranks = {
        {QStringLiteral("DEBUG"), 0},
        {QStringLiteral("INFO"), 1},
        {QStringLiteral("WARN"), 2},
        {QStringLiteral("ERROR"), 3},
        {QStringLiteral("FATAL"), 4},
    };
    return ranks.value(level.toUpper(), -1);
}

// Extracts the level tag from a line in WebDebugLogger's persisted format
// ("[<elapsed>] <LEVEL> <message>", written by handleMessage()). Returns an
// empty string for lines with no level tag.
inline QString lineLevel(const QString& line)
{
    static const QRegularExpression re(QStringLiteral(R"(^\[[^\]]*\]\s*([A-Za-z]+))"));
    const auto m = re.match(line);
    return m.hasMatch() ? m.captured(1).toUpper() : QString();
}

// True when `line` belongs to any of the given subsystems. `markers` are the
// BRACKETED tokens ("[Scale]", "[DE1]") that DecenzaLog::markerFilter() composes;
// an empty list matches nothing, which is what a caller asking for no subsystem
// should get rather than everything.
//
// Case-SENSITIVE, unlike filterLines()' `filter` below, and the asymmetry is
// deliberate. That one takes a free string a human typed and being forgiving is a
// kindness. A marker is a fixed token emitted by a macro: "[scale]" is not one,
// and matching it would only ever be a false positive on prose that happened to
// contain the word.
//
// Substring, never a pattern. A bracketed marker read as a regex is a character
// class — "[Scale]" would match any line containing S, c, a, l or e, i.e. very
// nearly every line — so this deliberately offers no regex mode to reach for.
//
// Lives here rather than beside its caller so that "a line belonging to subsystem
// X" has one definition shared by the connections-page views and the MCP tools.
// Two implementations of that predicate would be free to disagree about exactly
// the queries the markers were introduced to make answerable.
inline bool matchesAnyMarker(const QString& line, const QStringList& markers)
{
    for (const QString& marker : markers) {
        if (!marker.isEmpty() && line.contains(marker, Qt::CaseSensitive)) return true;
    }
    return false;
}

// The prefix a line carries, and which of the four grammars it is written in.
//
// Four is not a design, it is what accreted, and the census built on this is how
// a reader finds that out. `debug_get_log`'s description is generated from the
// marker registry, so it names the registered subsystems and is silent about
// every other family — an assistant that searches [Scale], gets a complete
// answer, and infers the log is marker-organised has been misled by a tool
// telling it only the true part. Reporting the other three kinds turns "this
// subsystem does not exist" into "this subsystem is not searchable by marker",
// which is a different and much cheaper mistake to recover from.
enum class PrefixKind { RegisteredMarker, UnregisteredBracket, ClassPrefix, None };

struct LinePrefix {
    PrefixKind kind = PrefixKind::None;
    QString token;  // "Scale", "MqttClient", … ; empty for None
};

inline LinePrefix linePrefix(const QString& line, const QSet<QString>& registeredTokens)
{
    // Skip the leading "[<time>] LEVEL " field; what follows is the message.
    static const QRegularExpression head(QStringLiteral(R"(^\[[^\]]*\]\s*[A-Za-z]+\s+)"));
    const auto hm = head.match(line);
    if (!hm.hasMatch()) return {};
    const QString msg = line.mid(hm.capturedLength(0));

    static const QRegularExpression bracket(QStringLiteral(R"(^\[([A-Za-z][A-Za-z0-9 _.\-]*)\])"));
    const auto bm = bracket.match(msg);
    if (bm.hasMatch()) {
        const QString tok = bm.captured(1);
        return {registeredTokens.contains(tok) ? PrefixKind::RegisteredMarker
                                               : PrefixKind::UnregisteredBracket, tok};
    }
    static const QRegularExpression cls(QStringLiteral(R"(^([A-Z][A-Za-z0-9]*):\s)"));
    const auto cm = cls.match(msg);
    if (cm.hasMatch()) return {PrefixKind::ClassPrefix, cm.captured(1)};
    return {};
}

// Filters `lines` (whose element 0 is absolute line number `startLine` within
// the addressed range) by substring/regex `filter` and/or `minLevel`
// (inclusive threshold). A line must satisfy both when both are given.
// `filter`/`minLevel` empty means "no constraint". On an invalid `regex`
// pattern, `errorOut` (if non-null) is set and an empty list is returned.
inline QList<LineMatch> filterLines(const QStringList& lines, qsizetype startLine,
                                     const QString& filter, bool regexMode,
                                     const QString& minLevel, QString* errorOut = nullptr)
{
    QList<LineMatch> result;

    const bool hasFilter = !filter.isEmpty();
    QRegularExpression filterRe;
    if (hasFilter && regexMode) {
        filterRe = QRegularExpression(filter, QRegularExpression::CaseInsensitiveOption);
        if (!filterRe.isValid()) {
            if (errorOut) *errorOut = QStringLiteral("Invalid regex: ") + filterRe.errorString();
            return result;
        }
    }

    const bool hasMinLevel = !minLevel.isEmpty();
    const int minRank = hasMinLevel ? levelRank(minLevel) : -1;

    result.reserve(lines.size());
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString& line = lines[i];
        if (hasFilter) {
            const bool matches = regexMode
                ? filterRe.match(line).hasMatch()
                : line.contains(filter, Qt::CaseInsensitive);
            if (!matches) continue;
        }
        if (hasMinLevel && levelRank(lineLevel(line)) < minRank) continue;
        result.append({startLine + i, line, 1, startLine + i});
    }
    return result;
}

// Strips a leading "[<elapsed>] " field (WebDebugLogger's persisted line format —
// see lineLevel() above) so two lines that differ only in when they were logged
// compare equal. Lines with no such prefix (shot debug log lines, session
// markers) are returned unchanged.
//
// This said "[<HH:mm:ss.zzz>]", which is the CONSOLE pattern
// (qSetMessagePattern in main.cpp), not the persisted one — two different
// timestamps for the same message, and only the elapsed one ever reaches here.
// The regex is "[^]]*" so it worked either way; the comment was the part that
// misled.
inline QString stripTimestampPrefix(const QString& line)
{
    static const QRegularExpression re(QStringLiteral(R"(^\[[^\]]*\]\s*)"));
    const auto m = re.match(line);
    return m.hasMatch() ? line.mid(m.capturedLength(0)) : line;
}

// Collapses consecutive entries in `matches` whose text is equal once each
// line's own leading timestamp is stripped (see stripTimestampPrefix()) into
// one entry: `line`/`text` describe the first occurrence, `count` is the
// number of occurrences collapsed, `lastLine` is the absolute line number of
// the last occurrence. Non-consecutive occurrences of the same text (with a
// different entry in between) are NOT collapsed together — this is `uniq -c`,
// not `sort | uniq -c`. Deliberately compares only after stripping the
// timestamp, not after normalizing numbers elsewhere in the message, so two
// genuinely different events that happen to share a message template (e.g.
// two different shot ids in an otherwise-identical log line) are never
// merged — see design.md Decision 6.
inline QList<LineMatch> dedupeConsecutive(const QList<LineMatch>& matches)
{
    QList<LineMatch> result;
    QString lastKey;
    for (const auto& m : matches) {
        const QString key = stripTimestampPrefix(m.text);
        if (!result.isEmpty() && key == lastKey) {
            result.last().count++;
            result.last().lastLine = m.line;
        } else {
            result.append(m);
            lastKey = key;
        }
    }
    return result;
}

// Selects the page of `matches` to return. When `tail > 0`, returns the last
// `tail` entries (and `offset` is ignored — tail takes precedence). Otherwise
// returns the [offset, offset+limit) window.
inline QList<LineMatch> paginate(const QList<LineMatch>& matches, qsizetype offset,
                                  qsizetype limit, qsizetype tail)
{
    if (tail > 0) {
        const qsizetype start = qMax(qsizetype(0), matches.size() - tail);
        return matches.mid(start);
    }
    if (offset < 0 || offset >= matches.size()) return {};
    // Clamp here, not only at the callers. QList::mid() treats a NEGATIVE length as
    // "to the end", so an unclamped limit < 0 would silently return the whole log
    // from `offset` — an unbounded MCP response that looks like an ordinary page.
    // Both call sites currently qBound() it, which means the policy already exists
    // twice; this is the copy that cannot be forgotten.
    return matches.mid(offset, qMax(qsizetype(0), limit));
}

} // namespace McpLogFilter
