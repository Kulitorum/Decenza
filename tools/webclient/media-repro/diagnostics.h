#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class MediaDiagnostics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    Q_INVOKABLE int videoElementCount() const;
    Q_INVOKABLE QString browserMediaReport() const;
};
