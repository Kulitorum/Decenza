#include "fastlinerenderer.h"

FastLineRenderer::FastLineRenderer(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    m_points.reserve(MAX_POINTS);
}

void FastLineRenderer::setColor(const QColor& color) {
    if (m_color == color) return;
    m_color = color;
    m_materialDirty = true;
    update();
    emit colorChanged();
}

void FastLineRenderer::setLineWidth(float width) {
    if (qFuzzyCompare(m_lineWidth, width)) return;
    m_lineWidth = width;
    m_geometryDirty = true;
    update();
    emit lineWidthChanged();
}

void FastLineRenderer::setMinX(double v) {
    if (qFuzzyCompare(m_minX, v)) return;
    m_minX = v;
    m_geometryDirty = true;
    update();
    emit minXChanged();
}

void FastLineRenderer::setMaxX(double v) {
    if (qFuzzyCompare(m_maxX, v)) return;
    m_maxX = v;
    m_geometryDirty = true;
    update();
    emit maxXChanged();
}

void FastLineRenderer::setMinY(double v) {
    if (qFuzzyCompare(m_minY, v)) return;
    m_minY = v;
    m_geometryDirty = true;
    update();
    emit minYChanged();
}

void FastLineRenderer::setMaxY(double v) {
    if (qFuzzyCompare(m_maxY, v)) return;
    m_maxY = v;
    m_geometryDirty = true;
    update();
    emit maxYChanged();
}

void FastLineRenderer::appendPoint(double x, double y) {
    if (m_pointCount < MAX_POINTS) {
        if (m_pointCount < m_points.size()) {
            m_points[m_pointCount] = QPointF(x, y);
        } else {
            m_points.append(QPointF(x, y));
        }
        m_pointCount++;
        m_geometryDirty = true;
        update();
    }
}

void FastLineRenderer::clear() {
    m_pointCount = 0;
    m_geometryDirty = true;
    update();
}

void FastLineRenderer::setPoints(const QVector<QPointF>& points) {
    m_pointCount = qMin(points.size(), MAX_POINTS);
    m_points = points;
    if (m_points.size() > MAX_POINTS)
        m_points.resize(MAX_POINTS);
    m_geometryDirty = true;
    update();
}

QSGNode* FastLineRenderer::updatePaintNode(QSGNode* node, UpdatePaintNodeData*) {
    auto* gnode = static_cast<QSGGeometryNode*>(node);

    if (!gnode) {
        // First call: create geometry node with pre-allocated buffer
        gnode = new QSGGeometryNode();

        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), MAX_POINTS);
        geometry->setDrawingMode(QSGGeometry::DrawLineStrip);
        geometry->setLineWidth(m_lineWidth);
        geometry->setVertexDataPattern(QSGGeometry::StreamPattern);

        gnode->setGeometry(geometry);
        gnode->setFlag(QSGNode::OwnsGeometry);

        auto* material = new QSGFlatColorMaterial();
        material->setColor(m_color);
        gnode->setMaterial(material);
        gnode->setFlag(QSGNode::OwnsMaterial);

        m_geometryDirty = true;
        m_materialDirty = false;
    }

    // Update material color if changed
    if (m_materialDirty) {
        auto* material = static_cast<QSGFlatColorMaterial*>(gnode->material());
        material->setColor(m_color);
        gnode->markDirty(QSGNode::DirtyMaterial);
        m_materialDirty = false;
    }

    // Update line width if changed
    auto* geometry = gnode->geometry();
    if (m_lineWidth != geometry->lineWidth()) {
        geometry->setLineWidth(m_lineWidth);
    }

    // Transform data-space points to pixel-space and write to pre-allocated buffer
    if (m_geometryDirty) {
        auto* v = geometry->vertexDataAsPoint2D();
        const float w = static_cast<float>(width());
        const float h = static_cast<float>(height());
        const double rangeX = m_maxX - m_minX;
        const double rangeY = m_maxY - m_minY;

        if (rangeX > 0 && rangeY > 0 && m_pointCount > 0) {
            const double scaleX = w / rangeX;
            const double scaleY = h / rangeY;

            for (int i = 0; i < m_pointCount; ++i) {
                float px = static_cast<float>((m_points[i].x() - m_minX) * scaleX);
                float py = h - static_cast<float>((m_points[i].y() - m_minY) * scaleY);
                v[i].set(px, py);
            }

            // Fill remaining with last valid point (degenerate zero-length segments)
            QSGGeometry::Point2D last = v[m_pointCount - 1];
            for (int i = m_pointCount; i < MAX_POINTS; ++i) {
                v[i] = last;
            }
        } else {
            // No data or invalid range: all vertices at origin (invisible)
            for (int i = 0; i < MAX_POINTS; ++i) {
                v[i].set(0.0f, 0.0f);
            }
        }

        geometry->markVertexDataDirty();
        gnode->markDirty(QSGNode::DirtyGeometry);
        m_geometryDirty = false;
    }

    return gnode;
}
