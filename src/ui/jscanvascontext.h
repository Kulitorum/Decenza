#ifndef JSCANVASCONTEXT_H
#define JSCANVASCONTEXT_H

#include <QObject>
#include <QColor>
#include <QList>
#include <QVector>
#include <QVariant>

#include <QtCanvasPainter/qcanvasgradient.h>

QT_BEGIN_NAMESPACE
class QCanvasPainter;
QT_END_NAMESPACE

class JsCanvasGradient;

// Recorded drawing command. Tag + small POD payload — fixed size keeps
// QVector<DrawCmd> reallocation-free per call within a frame.
struct DrawCmd {
    enum class Op : quint16 {
        BeginPath, ClosePath,
        MoveTo, LineTo,
        Arc, Ellipse,
        Fill, Stroke,
        FillRect, ClearRect, StrokeRect,
        Save, Restore, Reset,
        SetFillColor, SetStrokeColor,
        SetFillBrush, SetStrokeBrush,
        SetLineWidth, SetLineCap, SetGlobalAlpha,
    };

    Op op;
    quint8 lineCap;        // 0=butt 1=round 2=square (used by SetLineCap)
    quint8 anticlockwise;  // 0/1 (used by Arc)
    quint8 _pad;
    qsizetype brushId;     // index into m_brushes (used by SetFill/StrokeBrush)
    float a, b, c, d, e, f; // generic floats (coords / radii / angles)
    QRgb rgba;             // packed color (used by SetFill/StrokeColor)
};

// Gradient/brush data referenced by SetFill/StrokeBrush commands.
// Held in a parallel QVector<BrushSpec> indexed by id; ids reset per frame.
struct BrushSpec {
    enum class Type : quint8 { Linear, Radial };
    Type type;
    float x0, y0, r0;
    float x1, y1, r1;       // r0/r1 only meaningful for Radial
    QList<QCanvasGradientStop> stops;
};

// JS-callable 2D drawing context. QML's onPaint handler records into this;
// the renderer replays the buffer onto a QCanvasPainter on the render thread.
class JsCanvasContext : public QObject
{
    Q_OBJECT
    // QML assignment syntax (`ctx.fillStyle = grad`) needs a Q_PROPERTY,
    // but the recorded buffer is the source of truth — readers never need
    // to query state back. Stub READs satisfy the moc requirement.
    Q_PROPERTY(QVariant fillStyle READ dummyVariant WRITE setFillStyle)
    Q_PROPERTY(QVariant strokeStyle READ dummyVariant WRITE setStrokeStyle)
    Q_PROPERTY(float lineWidth READ dummyFloat WRITE setLineWidth)
    Q_PROPERTY(QString lineCap READ dummyString WRITE setLineCap)
    Q_PROPERTY(float globalAlpha READ dummyFloat WRITE setGlobalAlpha)

public:
    explicit JsCanvasContext(QObject *parent = nullptr);

    QVariant dummyVariant() const { return {}; }
    float dummyFloat() const { return 0.0f; }
    QString dummyString() const { return {}; }

    // Path
    Q_INVOKABLE void beginPath();
    Q_INVOKABLE void closePath();
    Q_INVOKABLE void moveTo(float x, float y);
    Q_INVOKABLE void lineTo(float x, float y);
    Q_INVOKABLE void arc(float cx, float cy, float r, float a0, float a1, bool anticlockwise = false);
    // QML-Canvas-style bounding-box ellipse: (x, y, w, h)
    Q_INVOKABLE void ellipse(float x, float y, float w, float h);

    // Fill/stroke
    Q_INVOKABLE void fill();
    Q_INVOKABLE void stroke();
    Q_INVOKABLE void fillRect(float x, float y, float w, float h);
    Q_INVOKABLE void clearRect(float x, float y, float w, float h);
    Q_INVOKABLE void strokeRect(float x, float y, float w, float h);

    // State
    Q_INVOKABLE void save();
    Q_INVOKABLE void restore();
    Q_INVOKABLE void reset();

    // Gradient factories
    Q_INVOKABLE QObject* createLinearGradient(float x0, float y0, float x1, float y1);
    Q_INVOKABLE QObject* createRadialGradient(float x0, float y0, float r0, float x1, float y1, float r1);

    // Property setters (also exposed via Q_PROPERTY for QML assignment)
    void setFillStyle(const QVariant &v);
    void setStrokeStyle(const QVariant &v);
    void setLineWidth(float w);
    void setLineCap(const QString &cap);
    void setGlobalAlpha(float a);

    // Buffer access for the renderer (called only on synchronizeData(), main blocked)
    QVector<DrawCmd> &cmds() { return m_cmds; }
    QVector<BrushSpec> &brushes() { return m_brushes; }

    // Reset buffers and gradient children for a new recording pass.
    // Keeps QVector capacity to avoid reallocations across frames.
    void resetForNextFrame();

    // Used by JsCanvasGradient::addColorStop to write back into the buffer.
    BrushSpec &brushAt(qsizetype id) { return m_brushes[id]; }

private:
    void setStyleStream(const QVariant &v, DrawCmd::Op colorOp, DrawCmd::Op brushOp);
    qsizetype newBrush(BrushSpec::Type type, float x0, float y0, float r0, float x1, float y1, float r1);

    QVector<DrawCmd> m_cmds;
    QVector<BrushSpec> m_brushes;
    QList<JsCanvasGradient*> m_gradients;  // parented to this; cleared per frame
};

// JS-callable gradient handle. Holds an id into the parent ctx's m_brushes.
class JsCanvasGradient : public QObject
{
    Q_OBJECT

public:
    JsCanvasGradient(JsCanvasContext *ctx, qsizetype brushId);

    Q_INVOKABLE void addColorStop(float position, const QVariant &color);

    qsizetype brushId() const { return m_brushId; }

private:
    JsCanvasContext *m_ctx;
    qsizetype m_brushId;
};

#endif // JSCANVASCONTEXT_H
