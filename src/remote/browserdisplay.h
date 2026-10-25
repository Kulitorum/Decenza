#pragma once

#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QtQmlIntegration/qqmlintegration.h>

class QPointingDevice;

// Local browser presentation and input; never changes the native host's window.
class BrowserDisplay final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool isBrowser READ isBrowser CONSTANT)
    Q_PROPERTY(bool fullscreenSupported READ fullscreenSupported CONSTANT)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(bool standalone READ standalone CONSTANT)

public:
    explicit BrowserDisplay(QObject* parent = nullptr);
    ~BrowserDisplay() override;

    bool isBrowser() const;
    bool fullscreenSupported() const;
    bool fullscreen() const;
    bool standalone() const;
    Q_INVOKABLE void toggleFullscreen();

    static void browserChanged();
    static void browserFailed();

signals:
    void fullscreenChanged();
    void fullscreenFailed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool sendPixelWheel(Qt::ScrollPhase phase, const QPoint& delta);
    QPointer<QObject> m_wheelObserver;
    QPointer<QObject> m_wheelTarget;
    QPointF m_wheelPosition;
    QPointF m_wheelGlobalPosition;
    Qt::MouseButtons m_wheelButtons = Qt::NoButton;
    Qt::KeyboardModifiers m_wheelModifiers = Qt::NoModifier;
    const QPointingDevice* m_wheelDevice = nullptr;
    quint64 m_wheelTimestamp = 0;
    bool m_wheelInverted = false;
    static QPointer<BrowserDisplay> s_instance;

private slots:
    void finishWheelGesture();
};
