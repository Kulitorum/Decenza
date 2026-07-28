#ifndef JSCANVASPAINTERITEM_H
#define JSCANVASPAINTERITEM_H

#include <QtCanvasPainter/qcanvaspainteritem.h>

#include "jscanvascontext.h"
#include <QtQml/qqmlregistration.h>

// QML element exposing a Canvas-like JS surface (`onPaint`, `requestPaint()`)
// backed by Qt 6.11's GPU-accelerated QCanvasPainter. The QML handler records
// drawing commands into a JsCanvasContext; the renderer replays them on the
// scene-graph render thread via QCanvasPainter.
class JsCanvasPainterItem : public QCanvasPainterItem
{
    Q_OBJECT
    // Compile-time registration. A runtime qmlRegisterType<>() in main.cpp is invisible to
    // qmltyperegistrar, so the type never reaches Decenza.qmltypes and qmllint reports every
    // use of it as "was not found. Did you add all imports and dependencies?" — 16 such
    // warnings across four QML files for these four types, all from this one cause.
    QML_ELEMENT


public:
    explicit JsCanvasPainterItem(QQuickItem *parent = nullptr);

    Q_INVOKABLE void requestPaint();

    // Accessed by the renderer during synchronizeData() while the main thread
    // is blocked — safe to swap buffers directly.
    JsCanvasContext &ctx() { return m_ctx; }

protected:
    QCanvasPainterItemRenderer *createItemRenderer() const override;

Q_SIGNALS:
    // Emitted on the main thread before update() so QML's `onPaint` can record
    // draw commands into the supplied JsCanvasContext.
    // Typed, not QObject*. The concrete type is what lets qmllint check the ~66 canvas calls in
    // CupFillView.qml; as QObject* every one of them resolved to a missing member.
    void paint(JsCanvasContext *ctx);

private:
    JsCanvasContext m_ctx;
    bool m_loggedInit = false;  // one-shot RHI-backend log on first requestPaint() with a window
};

#endif // JSCANVASPAINTERITEM_H
