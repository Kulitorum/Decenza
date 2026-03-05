#pragma once

#include <QQuickPaintedItem>
#include <QImage>
#include <QTimer>
#include <QVector>
#include <QColor>
#include <QRandomGenerator>
#include <QMutex>

// Attractor types
enum class AttractorType {
    Lorenz,
    Thomas,
    Aizawa,
    Halvorsen,
    Dadras,
    Chen,
    Rossler,
    Sprott,
    NumTypes
};

// Colormap types
enum class ColormapType {
    Inferno,
    Viridis,
    Magma,
    Plasma,
    NumTypes
};

class StrangeAttractorRenderer : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(int pointsPerFrame READ pointsPerFrame WRITE setPointsPerFrame NOTIFY pointsPerFrameChanged)
    Q_PROPERTY(int totalPoints READ totalPoints NOTIFY totalPointsChanged)
    Q_PROPERTY(QString attractorName READ attractorName NOTIFY attractorNameChanged)
    Q_PROPERTY(QString colormapName READ colormapName NOTIFY colormapNameChanged)

public:
    explicit StrangeAttractorRenderer(QQuickItem* parent = nullptr);
    ~StrangeAttractorRenderer() override;

    void paint(QPainter* painter) override;

    bool running() const { return m_running; }
    void setRunning(bool running);

    int pointsPerFrame() const { return m_pointsPerFrame; }
    void setPointsPerFrame(int points);

    int totalPoints() const { return m_totalPoints; }
    QString attractorName() const;
    QString colormapName() const;

    Q_INVOKABLE void reset();
    Q_INVOKABLE void randomize();

signals:
    void runningChanged();
    void pointsPerFrameChanged();
    void totalPointsChanged();
    void attractorNameChanged();
    void colormapNameChanged();

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private slots:
    void onRenderTick();

private:
    void initializeAttractor();
    void initializeColormap();
    void resizeBuffer();
    void updateImage();

    // Attractor step functions - return next point
    void stepLorenz(double& x, double& y, double& z, double dt);
    void stepThomas(double& x, double& y, double& z, double dt);
    void stepAizawa(double& x, double& y, double& z, double dt);
    void stepHalvorsen(double& x, double& y, double& z, double dt);
    void stepDadras(double& x, double& y, double& z, double dt);
    void stepChen(double& x, double& y, double& z, double dt);
    void stepRossler(double& x, double& y, double& z, double dt);
    void stepSprott(double& x, double& y, double& z, double dt);

    // Project 3D point to 2D screen coordinates
    QPointF project(double x, double y, double z);

    // HSL to RGB conversion
    QColor hslToRgb(double h, double s, double l);

private:
    bool m_running = false;
    int m_pointsPerFrame = 500;
    int m_totalPoints = 0;

    // Attractor state
    AttractorType m_attractorType = AttractorType::Lorenz;
    ColormapType m_colormapType = ColormapType::Inferno;
    double m_x = 0.1, m_y = 0.0, m_z = 0.0;
    double m_dt = 0.005;

    // Projection parameters (fixed camera)
    double m_scale = 1.0;
    double m_rotateX = 0.0;  // Rotation around X axis (radians)
    double m_rotateY = 0.0;  // Rotation around Y axis (radians)

    // Density buffer
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;
    QVector<float> m_density;
    float m_maxDensity = 0.0f;

    // Colormap (256 colors from black through palette to white)
    QVector<QRgb> m_colormap;
    double m_baseHue = 0.0;

    // Double-buffered output images (prevents race between render thread and main thread)
    QImage m_frontBuffer;  // Read by paint() on render thread
    QImage m_backBuffer;   // Written by updateImage() on main thread
    mutable QMutex m_imageMutex;  // Protects buffer swap

    // Animation timer
    QTimer* m_timer = nullptr;
    int m_frameCount = 0;
    int m_updateImageEvery = 5;  // Update visible image every N frames

    // Random generator
    QRandomGenerator m_rng;

    // Dynamic centering - warmup phase to find bounding box
    bool m_warmupPhase = true;
    int m_warmupPoints = 0;
    static constexpr int WARMUP_COUNT = 5000;
    double m_boundsMinX = 0.0, m_boundsMaxX = 0.0;
    double m_boundsMinY = 0.0, m_boundsMaxY = 0.0;
    double m_centerX = 0.0, m_centerY = 0.0;  // Computed center after warmup

    // Smoothed max density for gradual color scaling transitions
    float m_displayMaxDensity = 0.0f;  // Smoothly follows m_maxDensity
};
