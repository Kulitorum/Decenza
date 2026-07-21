#pragma once

#include <QHash>
#include <QList>
#include <QRegularExpression>
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
    qsizetype line;      // absolute line number of the first occurrence
    QString text;        // text of the first occurrence
    qsizetype count = 1; // consecutive occurrences collapsed into this entry
    qsizetype lastLine = -1; // absolute line number of the last occurrence
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

// Strips a leading "[<elapsed>] " field (WebDebugLogger's persisted line
// format — see lineLevel() above) so two lines that differ only in when they
// were logged compare equal. Lines with no such prefix (shot debug log lines,
// session markers) are returned unchanged.
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
    return matches.mid(offset, limit);
}

} // namespace McpLogFilter
