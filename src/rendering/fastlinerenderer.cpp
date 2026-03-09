#include "fastlinerenderer.h"
#include <cmath>

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
    m_pointCount = static_cast<int>(qMin(points.size(), qsizetype(MAX_POINTS)));
    m_points = points;
    if (m_points.size() > MAX_POINTS)
        m_points.resize(MAX_POINTS);
    m_geometryDirty = true;
    update();
}

void FastLineRenderer::itemChange(ItemChange change, const ItemChangeData& data) {
    if (change == ItemVisibleHasChanged && data.boolValue) {
        // When the item becomes visible again (e.g., StackView pop), force a repaint.
        // The scene graph may have destroyed our QSGNode while we were hidden,
        // and without an explicit update() call, updatePaintNode() won't be triggered.
        m_geometryDirty = true;
        update();
    }
    QQuickItem::itemChange(change, data);
}

// Triangle strip vertex count: 2 vertices per point (left/right of the line center)
static constexpr int MAX_VERTICES = FastLineRenderer::MAX_POINTS * 2;

QSGNode* FastLineRenderer::updatePaintNode(QSGNode* node, UpdatePaintNodeData*) {
    auto* gnode = static_cast<QSGGeometryNode*>(node);

    if (!gnode) {
        gnode = new QSGGeometryNode();

        auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), MAX_VERTICES);
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
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

    if (m_materialDirty) {
        auto* material = static_cast<QSGFlatColorMaterial*>(gnode->material());
        material->setColor(m_color);
        gnode->markDirty(QSGNode::DirtyMaterial);
        m_materialDirty = false;
    }

    if (m_geometryDirty) {
        auto* geometry = gnode->geometry();
        auto* v = geometry->vertexDataAsPoint2D();
        const float w = static_cast<float>(width());
        const float h = static_cast<float>(height());
        const double rangeX = m_maxX - m_minX;
        const double rangeY = m_maxY - m_minY;
        const float halfWidth = m_lineWidth * 0.5f;

        if (rangeX > 0 && rangeY > 0 && m_pointCount > 1) {
            const double scaleX = w / rangeX;
            const double scaleY = h / rangeY;

            // Convert data points to pixel coordinates in a temp buffer
            struct Pt { float x, y; };
            QVarLengthArray<Pt, 1024> px(m_pointCount);
            for (int i = 0; i < m_pointCount; ++i) {
                px[i].x = static_cast<float>((m_points[i].x() - m_minX) * scaleX);
                px[i].y = h - static_cast<float>((m_points[i].y() - m_minY) * scaleY);
            }

            // Generate triangle strip: for each point, emit two vertices offset
            // perpendicular to the line direction by halfWidth
            int vi = 0;
            for (int i = 0; i < m_pointCount; ++i) {
                float nx, ny;
                if (i == 0) {
                    // First point: use direction to next point
                    float dx = px[1].x - px[0].x;
                    float dy = px[1].y - px[0].y;
                    float len = std::sqrt(dx * dx + dy * dy);
                    if (len < 1e-6f) len = 1.0f;
                    nx = -dy / len;
                    ny = dx / len;
                } else if (i == m_pointCount - 1) {
                    // Last point: use direction from previous point
                    float dx = px[i].x - px[i - 1].x;
                    float dy = px[i].y - px[i - 1].y;
                    float len = std::sqrt(dx * dx + dy * dy);
                    if (len < 1e-6f) len = 1.0f;
                    nx = -dy / len;
                    ny = dx / len;
                } else {
                    // Middle points: average the normals of adjacent segments (miter join)
                    float dx1 = px[i].x - px[i - 1].x;
                    float dy1 = px[i].y - px[i - 1].y;
                    float len1 = std::sqrt(dx1 * dx1 + dy1 * dy1);
                    if (len1 < 1e-6f) len1 = 1.0f;
                    float nx1 = -dy1 / len1;
                    float ny1 = dx1 / len1;

                    float dx2 = px[i + 1].x - px[i].x;
                    float dy2 = px[i + 1].y - px[i].y;
                    float len2 = std::sqrt(dx2 * dx2 + dy2 * dy2);
                    if (len2 < 1e-6f) len2 = 1.0f;
                    float nx2 = -dy2 / len2;
                    float ny2 = dx2 / len2;

                    nx = (nx1 + nx2) * 0.5f;
                    ny = (ny1 + ny2) * 0.5f;
                    float nlen = std::sqrt(nx * nx + ny * ny);
                    if (nlen < 1e-6f) { nx = nx1; ny = ny1; }
                    else {
                        // Scale miter normal so the perpendicular offset equals halfWidth.
                        // dot(avgNormal, segNormal) gives the cosine of the half-angle;
                        // dividing by it corrects the miter length. Clamp to avoid spikes.
                        float dot = nx * nx1 + ny * ny1;
                        float miterLen = (dot > 0.25f) ? (nlen / dot) : 2.0f;
                        if (miterLen > 2.0f) miterLen = 2.0f;
                        nx = nx / nlen * miterLen;
                        ny = ny / nlen * miterLen;
                    }
                }

                v[vi].set(px[i].x + nx * halfWidth, px[i].y + ny * halfWidth);
                v[vi + 1].set(px[i].x - nx * halfWidth, px[i].y - ny * halfWidth);
                vi += 2;
            }

            // Fill remaining vertices with degenerate (last point)
            QSGGeometry::Point2D last = v[vi - 1];
            for (int i = vi; i < MAX_VERTICES; ++i) {
                v[i] = last;
            }
        } else if (rangeX > 0 && rangeY > 0 && m_pointCount == 1) {
            // Single point: draw a small dot
            float px = static_cast<float>((m_points[0].x() - m_minX) * (w / rangeX));
            float py = h - static_cast<float>((m_points[0].y() - m_minY) * (h / rangeY));
            v[0].set(px - halfWidth, py - halfWidth);
            v[1].set(px + halfWidth, py - halfWidth);
            for (int i = 2; i < MAX_VERTICES; ++i) {
                v[i].set(px, py);
            }
        } else {
            for (int i = 0; i < MAX_VERTICES; ++i) {
                v[i].set(0.0f, 0.0f);
            }
        }

        geometry->markVertexDataDirty();
        gnode->markDirty(QSGNode::DirtyGeometry);
        m_geometryDirty = false;
    }

    return gnode;
}
