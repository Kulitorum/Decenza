#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

// Long-form per-tool documentation, kept OUT of the `tools/list` descriptions.
//
// A tool description has to carry what a client needs in order to CHOOSE the tool
// and fill its arguments, and nothing else: every byte of it is paid on every
// listing, by every client, forever. Output-field meanings, worked sequences and
// the interaction rules that used to make descriptions 700-2300 characters live
// here instead, and are read on demand — `get_agent_file(topic)` for clients that
// use tools, `decenza://tools/<topic>` for clients that use resources.
//
// Both surfaces derive their topic list from the resource directory, so adding a
// markdown file to resources/ai/tools/ (and to resources/ai.qrc) is the whole
// job — there is no list in code to update, and therefore none to forget.
namespace McpAgentDocs {

inline QString topicPath(const QString& topic)
{
    return QStringLiteral(":/ai/tools/%1.md").arg(topic);
}

inline QStringList availableTopics()
{
    QStringList topics;
    const QDir dir(QStringLiteral(":/ai/tools"));
    const QStringList files = dir.entryList(QStringList{QStringLiteral("*.md")}, QDir::Files,
                                            QDir::Name);
    topics.reserve(files.size());
    for (const QString& file : files)
        topics << file.left(file.size() - 3);  // strip ".md"
    return topics;
}

}  // namespace McpAgentDocs
