#pragma once

#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>

// Shared body of every QML_SINGLETON create() that publishes an instance main() owns.
//
// This lives in its own header rather than beside the registrations in contextsingletons_qml.h
// because it has a second caller (appinfo.h), and a second hand-written copy is how the three
// pre-existing copies came to disagree about what they report. The QML_* and Q_GADGET macros
// still have to appear literally in each struct — moc does not expand preprocessor macros when it
// looks for them, so the registrations themselves cannot be shared — but this part can be.
template <typename T>
T* decenzaPublishedSingleton(T* instance, QJSEngine* engine, const char* qmlName)
{
    // Checked, not asserted. QT_FORCE_ASSERTS is only defined for sanitizer builds
    // (CMakeLists.txt), so Q_ASSERT compiles out of a shipped Release — and this is the one
    // condition where that matters: with the instance unpublished, create() returns null, every
    // reference to the name reads undefined, and nothing says why.
    if (!instance) {
        qCritical("%s: QML asked for the singleton before main() published it. Every binding on "
                  "this name will be undefined. Publish it before QQmlEngine::load().", qmlName);
        return nullptr;
    }
    if (engine->thread() != instance->thread()) {
        qCritical("%s: the QML engine and the instance are on different threads; QML property "
                  "access would be unsafe.", qmlName);
        return nullptr;
    }
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}
