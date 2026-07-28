#pragma once

#include <QtQuick3D/QQuick3DGeometry>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

// Custom cylinder geometry with configurable sides
class PipeCylinderGeometry : public QQuick3DGeometry {
    Q_OBJECT
    // Compile-time registration; the module declares DEPENDENCIES QtQuick3D so qmllint can
    // follow the QQuick3DGeometry prototype. Without that dependency this resolves as
    // "used but it is not resolved" and is no better than the runtime call it replaced.
    QML_ELEMENT

    Q_PROPERTY(float radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(float length READ length WRITE setLength NOTIFY lengthChanged)
    Q_PROPERTY(int sides READ sides WRITE setSides NOTIFY sidesChanged)

public:
    explicit PipeCylinderGeometry(QQuick3DObject *parent = nullptr);

    float radius() const { return m_radius; }
    void setRadius(float radius);

    float length() const { return m_length; }
    void setLength(float length);

    int sides() const { return m_sides; }
    void setSides(int sides);

signals:
    void radiusChanged();
    void lengthChanged();
    void sidesChanged();

private:
    void updateGeometry();

    float m_radius = 8.0f;
    float m_length = 60.0f;
    int m_sides = 16;
};

// Custom 90-degree elbow geometry (quarter torus)
class PipeElbowGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(float pipeRadius READ pipeRadius WRITE setPipeRadius NOTIFY pipeRadiusChanged)
    Q_PROPERTY(float bendRadius READ bendRadius WRITE setBendRadius NOTIFY bendRadiusChanged)
    Q_PROPERTY(int sides READ sides WRITE setSides NOTIFY sidesChanged)
    Q_PROPERTY(int segments READ segments WRITE setSegments NOTIFY segmentsChanged)

public:
    explicit PipeElbowGeometry(QQuick3DObject *parent = nullptr);

    float pipeRadius() const { return m_pipeRadius; }
    void setPipeRadius(float radius);

    float bendRadius() const { return m_bendRadius; }
    void setBendRadius(float radius);

    int sides() const { return m_sides; }
    void setSides(int sides);

    int segments() const { return m_segments; }
    void setSegments(int segments);

signals:
    void pipeRadiusChanged();
    void bendRadiusChanged();
    void sidesChanged();
    void segmentsChanged();

private:
    void updateGeometry();

    float m_pipeRadius = 8.0f;
    float m_bendRadius = 12.0f;
    int m_sides = 16;
    int m_segments = 9;
};

// Custom end cap geometry (flat disc)
class PipeCapGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(float radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(int sides READ sides WRITE setSides NOTIFY sidesChanged)

public:
    explicit PipeCapGeometry(QQuick3DObject *parent = nullptr);

    float radius() const { return m_radius; }
    void setRadius(float radius);

    int sides() const { return m_sides; }
    void setSides(int sides);

signals:
    void radiusChanged();
    void sidesChanged();

private:
    void updateGeometry();

    float m_radius = 8.0f;
    int m_sides = 16;
};

// Custom sphere geometry with configurable resolution
class PipeSphereGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(float radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(int sides READ sides WRITE setSides NOTIFY sidesChanged)

public:
    explicit PipeSphereGeometry(QQuick3DObject *parent = nullptr);

    float radius() const { return m_radius; }
    void setRadius(float radius);

    int sides() const { return m_sides; }
    void setSides(int sides);

signals:
    void radiusChanged();
    void sidesChanged();

private:
    void updateGeometry();

    float m_radius = 8.0f;
    int m_sides = 16;
};
