#include "browserdisplay.h"

#include <QCoreApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QWheelEvent>

#ifdef Q_OS_WASM
#include <emscripten.h>

EM_JS(void, installFullscreenListener, (), {
    Module.decenzaFullscreenListener = () => {
        if (!document.fullscreenElement && !document.webkitFullscreenElement && window.screen.orientation?.unlock) {
            try { window.screen.orientation.unlock(); } catch (_) {}
        }
        Module._decenzaFullscreenChanged();
    };
    document.addEventListener('fullscreenchange', Module.decenzaFullscreenListener);
    document.addEventListener('webkitfullscreenchange', Module.decenzaFullscreenListener);
});

EM_JS(void, removeFullscreenListener, (), {
    document.removeEventListener('fullscreenchange', Module.decenzaFullscreenListener);
    document.removeEventListener('webkitfullscreenchange', Module.decenzaFullscreenListener);
    delete Module.decenzaFullscreenListener;
});

EM_JS(void, toggleBrowserFullscreen, (), {
    const active = document.fullscreenElement || document.webkitFullscreenElement;
    const target = active ? document : document.documentElement;
    const action = active ? document.exitFullscreen || document.webkitExitFullscreen
        : target.requestFullscreen || target.webkitRequestFullscreen;
    try {
        const result = action.call(target);
        Promise.resolve(result).then(() => {
            if (!active && (document.fullscreenElement || document.webkitFullscreenElement)
                    && window.screen.orientation?.lock)
                window.screen.orientation.lock('landscape').catch(() => {});
        }).catch(() => Module._decenzaFullscreenFailed());
    } catch (_) {
        Module._decenzaFullscreenFailed();
    }
});

extern "C" EMSCRIPTEN_KEEPALIVE void decenzaFullscreenChanged()
{
    BrowserDisplay::browserChanged();
}

extern "C" EMSCRIPTEN_KEEPALIVE void decenzaFullscreenFailed()
{
    BrowserDisplay::browserFailed();
}
#endif

QPointer<BrowserDisplay> BrowserDisplay::s_instance;

BrowserDisplay::BrowserDisplay(QObject* parent) : QObject(parent)
{
    s_instance = this;
#ifdef Q_OS_WASM
    installFullscreenListener();
    QCoreApplication::instance()->installEventFilter(this);
#endif
}

BrowserDisplay::~BrowserDisplay()
{
#ifdef Q_OS_WASM
    removeFullscreenListener();
#endif
}

bool BrowserDisplay::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Wheel)
        return false;
    auto* wheel = static_cast<QWheelEvent*>(event);
    if (auto* window = qobject_cast<QQuickWindow*>(watched);
        window && !m_wheelObserverWindows.contains(window)) {
        m_wheelObserverWindows.removeIf([](const QPointer<QObject>& seen) { return seen.isNull(); });
        m_wheelObserverWindows.append(window);
        if (auto* engine = qmlEngine(window)) {
            // Observe Qt's input lifecycle, without blocking controls. Qt's
            // WheelHandler owns phase-less input completion (qtdeclarative/
            // src/quick/handlers/qquickwheelhandler.cpp:114-131,457-473).
            // While a modal popup is open the overlay eats a wheel whose topmost
            // target is outside it (qtdeclarative/src/quicktemplates/
            // qquickoverlay.cpp:83-150), so the page observer sits just under
            // the overlay (z 1000001, :414). A second observer sits topmost
            // inside the overlay, which neither the overlay nor a popup filters
            // (qquickoverlay.cpp:108-114, qquickpopup.cpp:3325-3334).
            QQmlComponent component(engine);
            component.setData(R"QML(
                import QtQuick
                import QtQuick.Templates as T
                Item {
                    property bool inOverlay: false
                    anchors.fill: parent
                    z: inOverlay ? 2147483647 : 1000000
                    Accessible.ignored: true
                    signal gestureEnded()
                    Component.onCompleted: if (inOverlay) parent = T.Overlay.overlay
                    WheelHandler {
                        id: vertical
                        target: null; blocking: false
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onActiveChanged: if (!active && !horizontal.active) parent.gestureEnded()
                    }
                    WheelHandler {
                        id: horizontal
                        target: null; blocking: false; orientation: Qt.Horizontal
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onActiveChanged: if (!active && !vertical.active) parent.gestureEnded()
                    }
                }
            )QML", QUrl());
            for (const bool inOverlay : {false, true}) {
                QObject* observer = component.createWithInitialProperties({
                    {QStringLiteral("parent"), QVariant::fromValue(window->contentItem())},
                    {QStringLiteral("inOverlay"), inOverlay}});
                Q_ASSERT_X(observer, "BrowserDisplay", qPrintable(component.errorString()));
                if (!observer)
                    continue;
                observer->setParent(window->contentItem());
                connect(observer, SIGNAL(gestureEnded()), this, SLOT(finishWheelGesture()));
            }
        }
    }
    if (wheel->phase() != Qt::NoScrollPhase || wheel->pixelDelta().isNull()
        || !watched->inherits("QQuickFlickable") || !watched->property("interactive").toBool())
        return false;
    auto* item = qobject_cast<QQuickItem*>(watched);
    if (!item)
        return false;
    for (auto* parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (parent->inherits("QQuickScrollView")) {
            if (!parent->property("wheelEnabled").toBool())
                return false;
            break;
        }
    }

    // Wasm lacks scroll phases (qtbase/src/plugins/platforms/wasm/qwasmwindow.cpp:965-970).
    // Supply them at the receiving Flickable, after child controls handled input,
    // to reuse Qt's pixel path, moving state and snapping (qtdeclarative/src/quick/
    // items/qquickflickable.cpp:1725-1763,1908-1938). Reassess with QTBUG-141540.
    const QPointer<QObject> target(watched);
    const QPoint delta = wheel->pixelDelta();
    if (m_wheelTarget != watched)
        finishWheelGesture();
    if (!target) {
        wheel->accept();
        return true;
    }
    const bool beginning = !m_wheelTarget;
    if (beginning)
        QMetaObject::invokeMethod(target, "cancelFlick", Qt::DirectConnection);
    if (!target) {
        wheel->accept();
        return true;
    }
    m_wheelTarget = target;
    m_wheelPosition = wheel->position();
    m_wheelGlobalPosition = wheel->globalPosition();
    m_wheelButtons = wheel->buttons();
    m_wheelModifiers = wheel->modifiers();
    m_wheelDevice = wheel->pointingDevice();
    m_wheelInverted = wheel->inverted();
    m_wheelTimestamp = qMax(m_wheelTimestamp, quint64(wheel->timestamp()));
    const auto position = [](QObject* object) {
        return QPointF(object->property("contentX").toReal(), object->property("contentY").toReal());
    };
    const QPointF before = position(target);
    // Qt first acquires the drag, then establishes its offset before moving
    // (qquickflickable.cpp:1270-1274,1333-1340,1421-1432).
    bool accepted = false;
    if (beginning) {
        accepted = sendPixelWheel(Qt::ScrollBegin, delta);
        // A resumed drag can move immediately; only seed until movement starts.
        for (int i = 0; target && position(target) == before && i < 2; ++i)
            accepted = sendPixelWheel(Qt::ScrollUpdate, delta) || accepted;
    } else if (target) {
        accepted = sendPixelWheel(Qt::ScrollUpdate, delta);
    }
    const bool moved = target && before != position(target);
    if (!target || accepted || moved) {
        wheel->accept();
    } else {
        // End at a boundary so reversing direction has no accumulated scroll debt.
        finishWheelGesture();
        wheel->ignore();
    }
    return true;
}

bool BrowserDisplay::sendPixelWheel(Qt::ScrollPhase phase, const QPoint& delta)
{
    if (!m_wheelTarget)
        return false;
    QWheelEvent event(m_wheelPosition, m_wheelGlobalPosition, delta, {}, m_wheelButtons,
                      m_wheelModifiers, phase, m_wheelInverted,
                      Qt::MouseEventSynthesizedBySystem, m_wheelDevice);
    event.setTimestamp(++m_wheelTimestamp);
    QCoreApplication::sendEvent(m_wheelTarget, &event);
    return event.isAccepted();
}

void BrowserDisplay::finishWheelGesture()
{
    const QPointer<QObject> target = m_wheelTarget;
    // Clear admission before delivering End: movement handlers can navigate.
    m_wheelTarget = nullptr;
    if (!target)
        return;
    QWheelEvent event(m_wheelPosition, m_wheelGlobalPosition, {}, {}, m_wheelButtons,
                      m_wheelModifiers, Qt::ScrollEnd, m_wheelInverted,
                      Qt::MouseEventSynthesizedBySystem, m_wheelDevice);
    event.setTimestamp(++m_wheelTimestamp);
    QCoreApplication::sendEvent(target, &event);
}

bool BrowserDisplay::isBrowser() const
{
#ifdef Q_OS_WASM
    return true;
#else
    return false;
#endif
}

bool BrowserDisplay::fullscreenSupported() const
{
#ifdef Q_OS_WASM
    return EM_ASM_INT({ return !!(document.fullscreenEnabled || document.webkitFullscreenEnabled); });
#else
    return false;
#endif
}

bool BrowserDisplay::fullscreen() const
{
#ifdef Q_OS_WASM
    return EM_ASM_INT({ return !!(document.fullscreenElement || document.webkitFullscreenElement); });
#else
    return false;
#endif
}

bool BrowserDisplay::standalone() const
{
#ifdef Q_OS_WASM
    return EM_ASM_INT({ return !!(navigator.standalone || matchMedia('(display-mode: standalone)').matches); });
#else
    return false;
#endif
}

bool BrowserDisplay::remoteWebAccess() const
{
#ifdef Q_OS_WASM
    // The Funnel serves on 443. The host's own Tailscale also reaches this server
    // under a .ts.net name, but on its LAN port, which is in the address.
    return EM_ASM_INT({ return (location.hostname.endsWith('.ts.net') && location.port === "") ? 1 : 0; });
#else
    return false;
#endif
}

void BrowserDisplay::toggleFullscreen()
{
#ifdef Q_OS_WASM
    toggleBrowserFullscreen();
#endif
}

void BrowserDisplay::browserChanged()
{
    if (s_instance) emit s_instance->fullscreenChanged();
}

void BrowserDisplay::browserFailed()
{
    if (s_instance) emit s_instance->fullscreenFailed();
}
