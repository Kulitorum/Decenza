#pragma once

#include <QQuickItem>
#include <QColor>
#include <QPointF>
#include <QVector>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>

class FastLineRenderer : public QQuickItem {
    Q_OBJECT

    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(float lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(double minX READ minX WRITE setMinX NOTIFY minXChanged)
    Q_PROPERTY(double maxX READ maxX WRITE setMaxX NOTIFY maxXChanged)
    Q_PROPERTY(double minY READ minY WRITE setMinY NOTIFY minYChanged)
    Q_PROPERTY(double maxY READ maxY WRITE setMaxY NOTIFY maxYChanged)

public:
    explicit FastLineRenderer(QQuickItem* parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor& color);

    float lineWidth() const { return m_lineWidth; }
    void setLineWidth(float width);

    double minX() const { return m_minX; }
    void setMinX(double v);
    double maxX() const { return m_maxX; }
    void setMaxX(double v);
    double minY() const { return m_minY; }
    void setMinY(double v);
    double maxY() const { return m_maxY; }
    void setMaxY(double v);

    // Called by ShotDataModel - fast, just appends to internal vector
    Q_INVOKABLE void appendPoint(double x, double y);
    Q_INVOKABLE void clear();
    // Bulk load for viewing completed shots on page re-entry
    void setPoints(const QVector<QPointF>& points);

signals:
    void colorChanged();
    void lineWidthChanged();
    void minXChanged();
    void maxXChanged();
    void minYChanged();
    void maxYChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* node, UpdatePaintNodeData*) override;

private:
    QVector<QPointF> m_points;  // Data-space coordinates
    int m_pointCount = 0;
    QColor m_color = Qt::white;
    float m_lineWidth = 2.0f;
    double m_minX = 0, m_maxX = 1, m_minY = 0, m_maxY = 1;
    bool m_geometryDirty = true;
    bool m_materialDirty = true;

    static constexpr int MAX_POINTS = 700;  // 2 min at 5Hz + margin
};
