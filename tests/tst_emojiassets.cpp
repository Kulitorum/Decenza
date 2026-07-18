// Verifies the SHIPPED emoji resource set, not the source directory.
//
// This distinction is the whole point. tst_textescaping stubs EmojiAssets in JavaScript and
// seeds it from resources/emoji/*.svg on disk; the real class reads QDir(":/emoji") — the Qt
// resource system, built from resources/emoji.qrc. Those two are never otherwise compared,
// so if emoji.qrc fell out of the build, or an asset were added without regenerating it,
// EmojiAssets::has() would return false for everything, Theme._emojiAssetPath() would return
// "", and EVERY emoji in the app would silently vanish — with tst_textescaping still green,
// because it reads disk.
//
// EmojiAssets::count() was documented as "used by tests to catch a resource system that came
// up empty" while having no callers at all. This file makes that claim true.

#include <QtTest>
#include <QDir>

#include "core/emojiassets.h"

class TestEmojiAssets : public QObject {
    Q_OBJECT

private slots:
    void init() { QTest::failOnWarning(); }

    void resourceSystemIsPopulated();
    void resourceSetMatchesTheSourceDirectory();
    void knownEmojiResolve();
    void unknownKeysDoNotResolve();
};

void TestEmojiAssets::resourceSystemIsPopulated()
{
    EmojiAssets assets;
    QVERIFY2(assets.count() > 4000,
             qPrintable(QStringLiteral("only %1 emoji in :/emoji — resources/emoji.qrc is "
                                       "probably missing from the build, which would strip "
                                       "every emoji in the app").arg(assets.count())));
}

// Catches the drift the qrc exists to prevent: an asset added to the directory but never
// added to emoji.qrc ships as "not bundled" and is silently stripped at runtime.
void TestEmojiAssets::resourceSetMatchesTheSourceDirectory()
{
    QDir src(QStringLiteral(DECENZA_SOURCE_DIR) + "/resources/emoji");
    const QStringList onDisk = src.entryList(QStringList{"*.svg"}, QDir::Files);
    QVERIFY2(!onDisk.isEmpty(), "source emoji directory is empty");

    EmojiAssets assets;
    QStringList missingFromResources;
    for (const QString& f : onDisk) {
        const QString key = f.left(f.size() - 4);
        if (!assets.has(key))
            missingFromResources << key;
    }

    QVERIFY2(missingFromResources.isEmpty(),
             qPrintable(QStringLiteral("%1 asset(s) exist on disk but are NOT in :/emoji — "
                                       "regenerate resources/emoji.qrc "
                                       "(python scripts/download_emoji.py twemoji --all). "
                                       "First few: %2")
                            .arg(missingFromResources.size())
                            .arg(missingFromResources.mid(0, 5).join(", "))));

    QCOMPARE(assets.count(), static_cast<int>(onDisk.size()));
}

void TestEmojiAssets::knownEmojiResolve()
{
    EmojiAssets assets;
    QVERIFY2(assets.has("2615"), "U+2615 HOT BEVERAGE is bundled and must resolve");
    QVERIFY2(assets.has("31-20e3"), "keycap 1 (FE0F stripped) must resolve");
    QVERIFY2(assets.has("a9"), "U+00A9 copyright must resolve");
}

void TestEmojiAssets::unknownKeysDoNotResolve()
{
    EmojiAssets assets;
    // Key format is hyphen-joined LOWERCASE hex with U+FE0F stripped and no extension.
    // Each of these is a well-typed call that must answer false — the invariant lives in a
    // comment, so these pin the shapes that would otherwise fail silently.
    QVERIFY2(!assets.has("2615.svg"), "extension must not be part of the key");
    QVERIFY2(!assets.has("2615"  " "), "whitespace must not resolve");
    QVERIFY2(!assets.has("31-fe0f-20e3"), "FE0F must be stripped from the key");
    QVERIFY2(!assets.has(""), "empty key must not resolve");
    QVERIFY2(!assets.has("1f322"), "U+1F322 has no upstream asset (used by tst_textescaping)");
}

QTEST_MAIN(TestEmojiAssets)
#include "tst_emojiassets.moc"
