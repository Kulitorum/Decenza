#include "keyboard.h"

#include <QGuiApplication>
#include <QInputMethod>

// QGuiApplication::inputMethod() is owned by the application and outlives every QML engine, so
// this class holds no pointer of its own and re-reads it. There is no null case in a running GUI
// app, but the guards cost nothing and keep the unit-test path (no QGuiApplication) from crashing.
Keyboard::Keyboard(QObject *parent)
    : QObject(parent)
{
    QInputMethod *im = QGuiApplication::inputMethod();
    if (!im)
        return;

    // Forwarded rather than exposed directly: QML binds to this object, and a binding on
    // QGuiApplication::inputMethod() is what this class exists to remove.
    connect(im, &QInputMethod::visibleChanged, this, &Keyboard::visibleChanged);
    connect(im, &QInputMethod::keyboardRectangleChanged, this, &Keyboard::rectangleChanged);
}

bool Keyboard::visible() const
{
    QInputMethod *im = QGuiApplication::inputMethod();
    return im && im->isVisible();
}

QRectF Keyboard::rectangle() const
{
    QInputMethod *im = QGuiApplication::inputMethod();
    return im ? im->keyboardRectangle() : QRectF();
}

void Keyboard::commit()
{
    if (QInputMethod *im = QGuiApplication::inputMethod())
        im->commit();
}

void Keyboard::hide()
{
    if (QInputMethod *im = QGuiApplication::inputMethod())
        im->hide();
}

void Keyboard::show()
{
    if (QInputMethod *im = QGuiApplication::inputMethod())
        im->show();
}
