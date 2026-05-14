#ifndef JSCANVASPAINTERITEM_H
#define JSCANVASPAINTERITEM_H

#include <QtCanvasPainter/qcanvaspainteritem.h>

#include "jscanvascontext.h"

// QML element exposing a Canvas-like JS surface (`onPaint`, `requestPaint()`)
// backed by Qt 6.11's GPU-accelerated QCanvasPainter. The QML handler records
// drawing commands into a JsCanvasContext; the renderer replays them on the
// scene-graph render thread via QCanvasPainter.
class JsCanvasPainterItem : public QCanvasPainterItem
{
    Q_OBJECT

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
    void paint(QObject *ctx);

private:
    JsCanvasContext m_ctx;
    bool m_loggedInit = false;  // one-shot RHI-backend log on first requestPaint() with a window
};

#endif // JSCANVASPAINTERITEM_H
