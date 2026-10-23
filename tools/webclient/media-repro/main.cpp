#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <cstdlib>

int main(int argc, char *argv[])
{
    qputenv("QT_WASM_ENABLE_ACCESSIBILITY", "1");
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.loadFromModule("QtWasmMediaLifecycleRepro", "Main");
    return app.exec();
}
