#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QStyleHints>
#include "core/bundledfontregistration.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <cstdlib>

int main(int argc, char* argv[])
{
    bool accessibilityRepro = false;
    bool listItemAccessibilityRepro = false;
#ifdef __EMSCRIPTEN__
    accessibilityRepro = EM_ASM_INT({
        return new URLSearchParams(window.location.search).has("accessibility-repro");
    });
    listItemAccessibilityRepro = EM_ASM_INT({
        return new URLSearchParams(window.location.search).get("accessibility-repro") === "list-item";
    });
    // Let Qt offer its screen-reader activation button on demand
    // (qtbase/src/plugins/platforms/wasm/qwasmaccessibility.cpp:106-120).
#endif
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle("Material");
#ifdef __EMSCRIPTEN__
    // Qt 6.12 Wasm places browser pixel deltas in angleDelta too
    // (qtbase/src/plugins/platforms/wasm/qwasmwindow.cpp:949-970).
    // Flickable's default wheel path scales that by wheelScrollLines * 24 / 120
    // (qtdeclarative/src/quick/items/qquickflickable.cpp:1776-1785).
    // Five lines preserves the incoming distance instead of the default 60%.
    app.styleHints()->setWheelScrollLines(5);
#endif
    const QVector<BundledFontRegistration::Font> uiFonts =
        BundledFontRegistration::registerUiFonts();
    const QString primaryFont = BundledFontRegistration::primaryFamily(uiFonts);
    const BundledFontRegistration::Font symbolFont =
        BundledFontRegistration::registerSymbolFont();
    if (!primaryFont.isEmpty()) {
        app.setFont(BundledFontRegistration::applicationFont(
            primaryFont, BundledFontRegistration::symbolFamily(symbolFont)));
    }
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
#ifdef __EMSCRIPTEN__
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
        [&app](QObject* object, const QUrl&) {
            if (auto* window = qobject_cast<QQuickWindow*>(object)) {
                QObject::connect(window, &QQuickWindow::frameSwapped, &app, [sent = false]() mutable {
                    if (sent) return;
                    sent = true;
                    EM_ASM({
                        window.dispatchEvent(new CustomEvent("decenza-first-frame", {
                            detail: { heapBytes: HEAPU8.buffer.byteLength }
                        }));
                    });
                });
            }
        });
#endif
    bool diagnostics = false;
#ifdef __EMSCRIPTEN__
    diagnostics = EM_ASM_INT({
        return new URLSearchParams(window.location.search).has("diagnostics");
    });
#endif
    const char* rootType = listItemAccessibilityRepro ? "ListItemEditableChildRepro"
        : accessibilityRepro ? "AccessibilityRepro"
        : diagnostics ? "DiagnosticsWindow" : "Main";
#ifdef __EMSCRIPTEN__
    if (!accessibilityRepro && !diagnostics) {
        auto* route = static_cast<char*>(EM_ASM_PTR({
            return stringToNewUTF8(window.location.hash.slice(1));
        }));
        if (route) {
            engine.setInitialProperties({{QStringLiteral("initialRoute"), QString::fromUtf8(route)}});
            std::free(route);
        }
    }
#endif
    engine.loadFromModule("Decenza", rootType);
    // Ignore Qt.quit()/Qt.exit() from shared QML in the browser. Disconnect after
    // load: Qt 6.12 installs these in its lazy engine initialization
    // (qtdeclarative/src/qml/qml/qqmlapplicationengine.cpp:29-52,102-123).
    QObject::disconnect(&engine, &QQmlApplicationEngine::quit, &app, nullptr);
    QObject::disconnect(&engine, &QQmlApplicationEngine::exit, &app, nullptr);
    return app.exec();
}
