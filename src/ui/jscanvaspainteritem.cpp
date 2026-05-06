#include "jscanvaspainteritem.h"

#include <QtCanvasPainter/qcanvaspainter.h>
#include <QtCanvasPainter/qcanvaspainteritemrenderer.h>
#include <QtCanvasPainter/qcanvaslineargradient.h>
#include <QtCanvasPainter/qcanvasradialgradient.h>

namespace {

QCanvasPainter::LineCap toLineCap(quint8 v)
{
    switch (v) {
    case 1: return QCanvasPainter::LineCap::Round;
    case 2: return QCanvasPainter::LineCap::Square;
    default: return QCanvasPainter::LineCap::Butt;
    }
}

void applyBrush(QCanvasPainter *p, const BrushSpec &s, bool fill)
{
    if (s.type == BrushSpec::Type::Linear) {
        QCanvasLinearGradient g(s.x0, s.y0, s.x1, s.y1);
        g.setStops(s.stops);
        if (fill) p->setFillStyle(g);
        else      p->setStrokeStyle(g);
    } else {
        // QCanvasRadialGradient is single-center with inner/outer radii.
        // Approximate the Canvas-2D two-circle form by using the outer
        // circle's center+radius and the inner circle's radius.
        QCanvasRadialGradient g(s.x1, s.y1, s.r1, s.r0);
        g.setStops(s.stops);
        if (fill) p->setFillStyle(g);
        else      p->setStrokeStyle(g);
    }
}

class JsCanvasPainterItemRenderer : public QCanvasPainterItemRenderer
{
public:
    void synchronize(QCanvasPainterItem *item) override
    {
        auto *self = static_cast<JsCanvasPainterItem*>(item);
        // Main thread is blocked here — safe to swap buffers directly.
        m_cmds.swap(self->ctx().cmds());
        m_brushes.swap(self->ctx().brushes());
    }

    void paint(QCanvasPainter *p) override
    {
        // Renderer holds the painter's state across frames, so reset() each
        // pass to mirror the Canvas semantics CupFillView relies on (the QML
        // handler calls ctx.reset() at the start; without our own reset here,
        // residual state from the previous swap would leak in).
        p->reset();

        for (const DrawCmd &c : std::as_const(m_cmds)) {
            switch (c.op) {
            case DrawCmd::Op::BeginPath: p->beginPath(); break;
            case DrawCmd::Op::ClosePath: p->closePath(); break;
            case DrawCmd::Op::MoveTo:    p->moveTo(c.a, c.b); break;
            case DrawCmd::Op::LineTo:    p->lineTo(c.a, c.b); break;
            case DrawCmd::Op::Arc:
                p->arc(c.a, c.b, c.c, c.d, c.e,
                       c.anticlockwise ? QCanvasPainter::PathWinding::CounterClockWise
                                       : QCanvasPainter::PathWinding::ClockWise);
                break;
            case DrawCmd::Op::Ellipse: {
                // QML-Canvas-style bounding box -> QCanvasPainter centers/radii.
                const float rx = c.c * 0.5f;
                const float ry = c.d * 0.5f;
                p->ellipse(c.a + rx, c.b + ry, rx, ry);
                break;
            }
            case DrawCmd::Op::Fill:        p->fill(); break;
            case DrawCmd::Op::Stroke:      p->stroke(); break;
            case DrawCmd::Op::FillRect:    p->fillRect(c.a, c.b, c.c, c.d); break;
            case DrawCmd::Op::ClearRect:   p->clearRect(c.a, c.b, c.c, c.d); break;
            case DrawCmd::Op::StrokeRect:  p->strokeRect(c.a, c.b, c.c, c.d); break;
            case DrawCmd::Op::Save:        p->save(); break;
            case DrawCmd::Op::Restore:     p->restore(); break;
            case DrawCmd::Op::Reset:       p->reset(); break;
            case DrawCmd::Op::SetFillColor:
                p->setFillStyle(QColor::fromRgba(c.rgba));
                break;
            case DrawCmd::Op::SetStrokeColor:
                p->setStrokeStyle(QColor::fromRgba(c.rgba));
                break;
            case DrawCmd::Op::SetFillBrush:
                if (c.brushId >= 0 && c.brushId < m_brushes.size())
                    applyBrush(p, m_brushes[c.brushId], /*fill=*/true);
                break;
            case DrawCmd::Op::SetStrokeBrush:
                if (c.brushId >= 0 && c.brushId < m_brushes.size())
                    applyBrush(p, m_brushes[c.brushId], /*fill=*/false);
                break;
            case DrawCmd::Op::SetLineWidth:   p->setLineWidth(c.a); break;
            case DrawCmd::Op::SetLineCap:     p->setLineCap(toLineCap(c.lineCap)); break;
            case DrawCmd::Op::SetGlobalAlpha: p->setGlobalAlpha(c.a); break;
            }
        }
    }

private:
    QVector<DrawCmd> m_cmds;
    QVector<BrushSpec> m_brushes;
};

} // namespace

JsCanvasPainterItem::JsCanvasPainterItem(QQuickItem *parent)
    : QCanvasPainterItem(parent)
    , m_ctx(this)
{
    // QCanvasPainterItem defaults to opaque black — match the Canvas
    // semantics CupFillView relies on (transparent canvas, premultiplied
    // composited over the parent layer).
    setFillColor(Qt::transparent);
    setAlphaBlending(true);
}

QCanvasPainterItemRenderer *JsCanvasPainterItem::createItemRenderer() const
{
    return new JsCanvasPainterItemRenderer();
}

void JsCanvasPainterItem::requestPaint()
{
    // Run the QML onPaint handler synchronously on the main thread so it
    // records into m_ctx, then schedule a render-thread sync+paint. Multiple
    // calls per frame coalesce inside Qt's update mechanism — only the latest
    // recording wins, which matches CupFillView's intent (it always redraws
    // from scratch on every animation tick).
    m_ctx.resetForNextFrame();
    Q_EMIT paint(&m_ctx);
    update();
}
