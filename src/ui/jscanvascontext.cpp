#include "jscanvascontext.h"

#include <QtCore/qmath.h>

namespace {

QColor toColor(const QVariant &v)
{
    if (v.canConvert<QColor>())
        return v.value<QColor>();
    if (v.typeId() == QMetaType::QString)
        return QColor::fromString(v.toString());
    return QColor();
}

} // namespace

JsCanvasContext::JsCanvasContext(QObject *parent)
    : QObject(parent)
{
    // Reserve a sensible starting capacity to avoid early reallocations.
    // CupFillView records on the order of a few hundred commands per frame.
    m_cmds.reserve(512);
    m_brushes.reserve(16);
    m_gradients.reserve(16);
}

void JsCanvasContext::beginPath()
{
    DrawCmd c{}; c.op = DrawCmd::Op::BeginPath; m_cmds.append(c);
}

void JsCanvasContext::closePath()
{
    DrawCmd c{}; c.op = DrawCmd::Op::ClosePath; m_cmds.append(c);
}

void JsCanvasContext::moveTo(float x, float y)
{
    DrawCmd c{}; c.op = DrawCmd::Op::MoveTo; c.a = x; c.b = y; m_cmds.append(c);
}

void JsCanvasContext::lineTo(float x, float y)
{
    DrawCmd c{}; c.op = DrawCmd::Op::LineTo; c.a = x; c.b = y; m_cmds.append(c);
}

void JsCanvasContext::arc(float cx, float cy, float r, float a0, float a1, bool anticlockwise)
{
    DrawCmd c{};
    c.op = DrawCmd::Op::Arc;
    c.a = cx; c.b = cy; c.c = r; c.d = a0; c.e = a1;
    c.anticlockwise = anticlockwise ? 1 : 0;
    m_cmds.append(c);
}

void JsCanvasContext::ellipse(float x, float y, float w, float h)
{
    DrawCmd c{};
    c.op = DrawCmd::Op::Ellipse;
    c.a = x; c.b = y; c.c = w; c.d = h;
    m_cmds.append(c);
}

void JsCanvasContext::fill()
{
    DrawCmd c{}; c.op = DrawCmd::Op::Fill; m_cmds.append(c);
}

void JsCanvasContext::stroke()
{
    DrawCmd c{}; c.op = DrawCmd::Op::Stroke; m_cmds.append(c);
}

void JsCanvasContext::fillRect(float x, float y, float w, float h)
{
    DrawCmd c{}; c.op = DrawCmd::Op::FillRect;
    c.a = x; c.b = y; c.c = w; c.d = h; m_cmds.append(c);
}

void JsCanvasContext::clearRect(float x, float y, float w, float h)
{
    DrawCmd c{}; c.op = DrawCmd::Op::ClearRect;
    c.a = x; c.b = y; c.c = w; c.d = h; m_cmds.append(c);
}

void JsCanvasContext::strokeRect(float x, float y, float w, float h)
{
    DrawCmd c{}; c.op = DrawCmd::Op::StrokeRect;
    c.a = x; c.b = y; c.c = w; c.d = h; m_cmds.append(c);
}

void JsCanvasContext::save()
{
    DrawCmd c{}; c.op = DrawCmd::Op::Save; m_cmds.append(c);
}

void JsCanvasContext::restore()
{
    DrawCmd c{}; c.op = DrawCmd::Op::Restore; m_cmds.append(c);
}

void JsCanvasContext::reset()
{
    DrawCmd c{}; c.op = DrawCmd::Op::Reset; m_cmds.append(c);
}

qsizetype JsCanvasContext::newBrush(BrushSpec::Type type,
                                    float x0, float y0, float r0,
                                    float x1, float y1, float r1)
{
    BrushSpec s;
    s.type = type;
    s.x0 = x0; s.y0 = y0; s.r0 = r0;
    s.x1 = x1; s.y1 = y1; s.r1 = r1;
    m_brushes.append(std::move(s));
    return m_brushes.size() - 1;
}

QObject *JsCanvasContext::createLinearGradient(float x0, float y0, float x1, float y1)
{
    const qsizetype id = newBrush(BrushSpec::Type::Linear, x0, y0, 0.0f, x1, y1, 0.0f);
    auto *g = new JsCanvasGradient(this, id);
    m_gradients.append(g);
    return g;
}

QObject *JsCanvasContext::createRadialGradient(float x0, float y0, float r0,
                                               float x1, float y1, float r1)
{
    const qsizetype id = newBrush(BrushSpec::Type::Radial, x0, y0, r0, x1, y1, r1);
    auto *g = new JsCanvasGradient(this, id);
    m_gradients.append(g);
    return g;
}

void JsCanvasContext::setStyleStream(const QVariant &v, DrawCmd::Op colorOp, DrawCmd::Op brushOp)
{
    if (auto *grad = qobject_cast<JsCanvasGradient*>(v.value<QObject*>())) {
        DrawCmd c{}; c.op = brushOp; c.brushId = grad->brushId();
        m_cmds.append(c);
        return;
    }
    QColor col = toColor(v);
    DrawCmd c{}; c.op = colorOp; c.rgba = col.rgba();
    m_cmds.append(c);
}

void JsCanvasContext::setFillStyle(const QVariant &v)
{
    setStyleStream(v, DrawCmd::Op::SetFillColor, DrawCmd::Op::SetFillBrush);
}

void JsCanvasContext::setStrokeStyle(const QVariant &v)
{
    setStyleStream(v, DrawCmd::Op::SetStrokeColor, DrawCmd::Op::SetStrokeBrush);
}

void JsCanvasContext::setLineWidth(float w)
{
    DrawCmd c{}; c.op = DrawCmd::Op::SetLineWidth; c.a = w;
    m_cmds.append(c);
}

void JsCanvasContext::setLineCap(const QString &cap)
{
    quint8 v = 0;
    if (cap == QStringLiteral("round")) v = 1;
    else if (cap == QStringLiteral("square")) v = 2;
    DrawCmd c{}; c.op = DrawCmd::Op::SetLineCap; c.lineCap = v;
    m_cmds.append(c);
}

void JsCanvasContext::setGlobalAlpha(float a)
{
    DrawCmd c{}; c.op = DrawCmd::Op::SetGlobalAlpha; c.a = a;
    m_cmds.append(c);
}

void JsCanvasContext::resetForNextFrame()
{
    // resize(0) keeps allocated capacity — no reallocation between frames.
    m_cmds.resize(0);
    m_brushes.resize(0);
    qDeleteAll(m_gradients);
    m_gradients.clear();
}

// ---------------- JsCanvasGradient ----------------

JsCanvasGradient::JsCanvasGradient(JsCanvasContext *ctx, qsizetype brushId)
    : QObject(ctx), m_ctx(ctx), m_brushId(brushId)
{
}

void JsCanvasGradient::addColorStop(float position, const QVariant &color)
{
    if (!m_ctx) return;
    BrushSpec &s = m_ctx->brushAt(m_brushId);
    QCanvasGradientStop stop;
    stop.position = position;
    stop.color = toColor(color);
    s.stops.append(stop);
}
