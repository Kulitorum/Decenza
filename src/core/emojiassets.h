#pragma once

#include <QObject>
#include <QSet>
#include <QString>

// Which emoji assets are actually bundled.
//
// The app ships the complete Twemoji set (~4,000 SVGs under :/emoji) and resolves emoji
// locally — there is no network fallback, by design: an emoji reaching the platform text
// renderer as a colour glyph crashes on macOS, so resolution cannot be best-effort.
//
// This exists because "bundled" is not the same as "everything". A codepoint from a Unicode
// revision newer than the pinned upstream, or a sequence upstream does not draw, has no asset.
// Theme.emojiToImage()/replaceEmojiWithImg() previously emitted qrc:/emoji/<key>.svg without
// checking, so those cases produced an image reference nothing could resolve — the emoji was
// neither drawn nor removed. Asking here first lets them strip instead.
//
// The set is built once from the Qt resource system, so there is no generated manifest to
// drift out of sync with the assets themselves.
class EmojiAssets : public QObject {
    Q_OBJECT

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

private:
    void ensureLoaded() const;

    mutable QSet<QString> m_keys;
    mutable bool m_loaded = false;
};
