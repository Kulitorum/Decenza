#pragma once

#include <QObject>
#include <QRectF>
#include <QtQml/qqmlregistration.h>

// The virtual keyboard / input method, as a type QML can be checked against.
//
// WHY THIS EXISTS
// ---------------
// QML reached the input method as `Qt.inputMethod.commit()`. That works, and it is not a bug —
// but qmllint types the `Qt` object's `inputMethod` as a bare QObject, so every one of the 108
// call sites across 27 files was an unchecked member access. `Qt.inputMethod.comit()` would have
// been indistinguishable from the correct spelling until a user hit the line, and the failure
// mode is the quiet one: the in-progress word is simply not committed.
//
// Which brings the second, better reason. CLAUDE.md carries this rule:
//
//     call Qt.inputMethod.commit() before reading any TextField.text from a button handler —
//     otherwise the in-progress word is lost on mobile
//
// That was a convention with no home in the code. It has one now: this class is where the rule
// is written down, and `Keyboard.commit()` is greppable in a way that a property lookup on a
// global object is not.
//
// It deliberately does NOT wrap all of QInputMethod — only what QML actually uses. Add a member
// when a call site needs it, not in anticipation.
class Keyboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // True while the platform's virtual keyboard is showing. Desktop reports false throughout.
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
    // The keyboard's rectangle in window coordinates; empty when it is not showing. Pages use it
    // to keep the focused field above the keyboard (see KeyboardAwareContainer).
    Q_PROPERTY(QRectF rectangle READ rectangle NOTIFY rectangleChanged)

public:
    explicit Keyboard(QObject *parent = nullptr);

    bool visible() const;
    QRectF rectangle() const;

public slots:
    // Commit the pre-edit text the IME is still holding.
    //
    // CALL THIS BEFORE READING TextField.text FROM A BUTTON HANDLER. Tapping a button does not
    // end IME composition on Android or iOS, so the word being typed is still pre-edit and is
    // NOT in `text` yet. Without this the last word is silently dropped — a bug that only
    // reproduces on a touch device with a predictive keyboard, which is why it kept coming back.
    void commit();

    void hide();
    void show();

signals:
    void visibleChanged();
    void rectangleChanged();
};
