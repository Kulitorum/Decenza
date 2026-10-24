#include "accessiblevaluechange.h"

#include <QtGui/qtguiglobal.h>
#if QT_CONFIG(accessibility)
#include <QAccessible>
#endif

#include <QJSEngine>
#include <QQmlEngine>
#include <QVariant>

AccessibleValueChange::AccessibleValueChange(QObject* parent)
    : QObject(parent)
{
}

AccessibleValueChange* AccessibleValueChange::create(QQmlEngine* engine, QJSEngine*)
{
    auto* helper = new AccessibleValueChange(engine);
    QJSEngine::setObjectOwnership(helper, QJSEngine::CppOwnership);
    return helper;
}

void AccessibleValueChange::notifyTextValueChanged(QObject* textField)
{
#if defined(__EMSCRIPTEN__) && QT_CONFIG(accessibility)
    if (!textField || !QAccessible::isActive())
        return;

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(textField);
    if (!iface || iface->role() != QAccessible::EditableText)
        return;

    // Use Qt's accessible display value, which masks password fields
    // (qtdeclarative/src/quick/accessible/qaccessiblequickitem.cpp:693-701).
    QAccessibleValueChangeEvent event(textField, iface->text(QAccessible::Value));
    QAccessible::updateAccessibility(&event);
#else
    Q_UNUSED(textField);
#endif
}

void AccessibleValueChange::notifyNumericValueChanged(QObject* control)
{
#if QT_CONFIG(accessibility)
    if (!control || !QAccessible::isActive())
        return;

    auto* iface = QAccessible::queryAccessibleInterface(control);
    auto* value = iface ? iface->valueInterface() : nullptr;
    if (!value)
        return;

    // Use the QObject overload: Qt 6.12's interface constructor stores its ID
    // in the child-index union even when object() is non-null (qtbase/src/gui/
    // accessible/qaccessible.h:366–380; qaccessible.cpp:1466–1477).
    QAccessibleValueChangeEvent event(control, value->currentValue());
    QAccessible::updateAccessibility(&event);
#else
    Q_UNUSED(control);
#endif
}
