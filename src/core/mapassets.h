#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

// Where the shot-map screensaver loads its earth textures from: qrc:/maps/ natively; in the
// Wasm client the host's /wasm/maps/<set hash>/, so the 1.15 MB of textures leave the runtime
// download and are fetched only when the screensaver shows. Engine-constructed, stateless.
class MapAssets : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString imageBase READ imageBase CONSTANT FINAL)

public:
    explicit MapAssets(QObject* parent = nullptr) : QObject(parent) {}

    QString imageBase() const;
};
