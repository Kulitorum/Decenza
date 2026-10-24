#include "accessiblevaluechange.h"

#if defined(__EMSCRIPTEN__)
#include <QtGui/qtguiglobal.h>
#if QT_CONFIG(accessibility)
#include <QAccessible>
#endif
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
    QAccessibleValueChangeEvent event(iface, iface->text(QAccessible::Value));
    QAccessible::updateAccessibility(&event);
#else
    Q_UNUSED(textField);
#endif
}
