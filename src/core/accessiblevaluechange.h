#pragma once

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>
#include <type_traits>

class QQmlEngine;
class QJSEngine;

// Shared text fields call this after their text changes. Qt Quick 6.12 emits
// TextUpdated for programmatic TextInput::setText() changes, but the Wasm
// accessibility backend does not handle that event in its editable-text path
// (qtdeclarative/src/quick/items/qquicktextinput.cpp:3885-3889;
// qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:802-841). Send the
// ValueChanged event that the existing Wasm handler does handle
// (qwasmaccessibility.cpp:814-817). Text notifications are a native no-op.
// Numeric notifications also cover custom Quick Items whose Accessible role is
// assigned after Qt's constructor-time value-signal hookup.
class AccessibleValueChange final : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(AccessibleValueChange)

public:
    static AccessibleValueChange* create(QQmlEngine* engine, QJSEngine* jsEngine);

    Q_INVOKABLE void notifyTextValueChanged(QObject* textField);
    Q_INVOKABLE void notifyNumericValueChanged(QObject* control);

private:
    explicit AccessibleValueChange(QObject* parent = nullptr);
};

static_assert(!std::is_default_constructible_v<AccessibleValueChange>,
              "AccessibleValueChange must be created by its QML engine factory.");
