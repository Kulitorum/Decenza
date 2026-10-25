#pragma once

#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QtQmlIntegration/qqmlintegration.h>

class QPointingDevice;

// Local browser presentation and input; never changes the native host's window.
// Not final: QML builds a default-constructible singleton as
// QQmlPrivate::QQmlElement<T>, which derives from it (qqmlprivate.h:105-106).
// MSVC rejects that for a final class; clang let it through.
class BrowserDisplay : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool isBrowser READ isBrowser CONSTANT)
    Q_PROPERTY(bool fullscreenSupported READ fullscreenSupported CONSTANT)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(bool standalone READ standalone CONSTANT)
    // Opened through Remote Web Access: a .ts.net host on the default port (the Funnel).
    Q_PROPERTY(bool remoteWebAccess READ remoteWebAccess CONSTANT)

public:
    explicit BrowserDisplay(QObject* parent = nullptr);
    ~BrowserDisplay() override;

    bool isBrowser() const;
    bool fullscreenSupported() const;
    bool fullscreen() const;
    bool standalone() const;
    bool remoteWebAccess() const;
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
