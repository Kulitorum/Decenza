#include "emojiassets.h"

#include <QDebug>
#include <QDir>

void EmojiAssets::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;

    // Enumerate the resource system rather than reading a generated manifest: the assets ARE
    // the source of truth, so there is nothing to keep in sync.
    QDir dir(QStringLiteral(":/emoji"));
    const QStringList files = dir.entryList(QStringList{QStringLiteral("*.svg")}, QDir::Files);
    m_keys.reserve(files.size());
    for (const QString& f : files)
        m_keys.insert(f.left(f.size() - 4));  // drop ".svg"

    if (m_keys.isEmpty()) {
        // Every emoji in the app would silently strip. That is a build/resource fault, not a
        // content one, so say so loudly rather than letting the UI quietly lose its emoji.
        qWarning() << "[Emoji] No assets found under :/emoji — every emoji will be stripped."
                   << "resources/emoji.qrc is probably missing from the build.";
    }
}

bool EmojiAssets::has(const QString& key) const
{
    ensureLoaded();
    return m_keys.contains(key);
}

int EmojiAssets::count() const
{
    ensureLoaded();
    return static_cast<int>(m_keys.size());
}
