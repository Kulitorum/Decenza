#include "core/diagnosticlogging.h"
#include "emojiassets.h"

#include <QFile>

#if defined(__EMSCRIPTEN__)
#include "core/browserorigin.h"
#include "decenza_image_sets.h"
#endif

void EmojiAssets::ensureLoaded() const
{
    if (m_loaded)
        return;
    m_loaded = true;

    QFile index(QStringLiteral(":/emoji-index.txt"));
    if (index.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> keys = index.readAll().split('\n');
        m_keys.reserve(keys.size());
        for (const QByteArray& key : keys) {
            if (!key.isEmpty())
                m_keys.insert(QString::fromLatin1(key));
        }
    }

    if (m_keys.isEmpty()) {
        // Every emoji in the app would silently strip. That is a build/resource fault, not a
        // content one, so say so loudly rather than letting the UI quietly lose its emoji.
        DIAG_WARN(APP, "EmojiAssets") << "No keys in :/emoji-index.txt — every emoji will be stripped."
                   << "The target is probably missing decenza_add_image_sets() (cmake/image_sets.cmake).";
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

QString EmojiAssets::imageBase() const
{
#if defined(__EMSCRIPTEN__)
    return hostImageSetBase(QStringLiteral("emoji"), DECENZA_EMOJI_SET_HASH);
#else
    return QStringLiteral("qrc:/emoji/");
#endif
}
