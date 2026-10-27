#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

// Which emoji assets are actually bundled, and where their images are loaded from.
//
// The app ships the complete Twemoji set (~4,000 SVGs under :/emoji). The native app resolves
// them locally; the Wasm client loads each image from its host on first display (imageBase).
// Neither ever hands an emoji to the platform text renderer as a colour glyph, which crashes
// on macOS, so a missing image renders blank rather than falling back to text.
//
// This exists because "bundled" is not the same as "everything". A codepoint from a Unicode
// revision newer than the pinned upstream, or a sequence upstream does not draw, has no asset.
// Theme.emojiToImage()/replaceEmojiWithImg() previously emitted qrc:/emoji/<key>.svg without
// checking, so those cases produced an image reference nothing could resolve — the emoji was
// neither drawn nor removed. Asking here first lets them strip instead.
//
// The set comes from :/emoji-index.txt, generated from resources/emoji.qrc at configure time
// (scripts/generate_emoji_index.py). The Wasm client embeds the index without the images, so
// both platforms answer identically; tst_emojiassets keeps the index equal to :/emoji.
class EmojiAssets : public QObject {
    Q_OBJECT

    // Compile-time QML registration, replacing setContextProperty("EmojiAssets", …). A context
    // property is invisible to qmllint, qmlcachegen and the language server. Full rationale in
    // src/controllers/maincontroller.h.
    //
    // No create() and no published instance, unlike the singletons that wrap objects main()
    // already owns: this class is default-constructible and holds no state anyone outside it
    // can observe, so the engine builds and owns it on first use. Two engines get two
    // instances, which is harmless — the state is the lazily-built m_keys cache below and its
    // m_loaded flag, both derived entirely from the Qt resource system, so a second instance
    // costs one extra index read and a second resident ~4,000-entry QSet. Nothing observable.
    QML_ELEMENT
    QML_SINGLETON

    // Prefix of every emoji image URL: qrc:/emoji/ natively; in the Wasm client the host's
    // /wasm/emoji/<set hash>/, versioned by the set's content so cached images survive updates.
    Q_PROPERTY(QString imageBase READ imageBase CONSTANT FINAL)

public:
    explicit EmojiAssets(QObject* parent = nullptr) : QObject(parent) {}

    // key: hyphen-joined lowercase hex codepoints with U+FE0F already stripped, matching the
    // asset filenames — "2615", "1f44d-1f3fd", "31-20e3". Stripping FE0F is required, not
    // cosmetic: upstream ships 31-20e3.svg and has no 31-fe0f-20e3.svg.
    //
    // Q_INVOKABLE rather than a property, and deliberately so despite the trap documented in
    // SettingsTheme::effectiveFontSizes: a binding calling an invokable records no dependency
    // and never re-evaluates. That is CORRECT here — the bundled set is fixed at build time and
    // cannot change while the app runs, so there is nothing to re-evaluate for.
    Q_INVOKABLE bool has(const QString& key) const;

    // Count of bundled assets. Used by tests to catch a resource system that came up empty,
    // which would otherwise silently strip every emoji in the app.
    Q_INVOKABLE int count() const;

    QString imageBase() const;

private:
    void ensureLoaded() const;

    mutable QSet<QString> m_keys;
    mutable bool m_loaded = false;
};
