#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

// Small QML adapter over the same PORTAL sample validation and bounds types used
// by ShotDataModel. Shared saved-shot graphs use this without importing the
// native live-capture singleton.
class PortalSampleTools : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(double ecAxisPadding READ ecAxisPadding CONSTANT FINAL)

public:
    explicit PortalSampleTools(QObject* parent = nullptr) : QObject(parent) {}

    double ecAxisPadding() const;
    Q_INVOKABLE QVariantList ecBounds(const QVariantList& samples) const;
};
