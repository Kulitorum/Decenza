#pragma once

#include <QVariantList>
#include <QString>
#include <functional>

class QObject;

// Narrow host-owned catalogue contract shared by theme settings and the
// screensaver manager. Implementations return only opaque IDs unless the
// native in-process preview explicitly requests a local path.
class BackgroundImageCatalog
{
public:
    virtual ~BackgroundImageCatalog() = default;
    virtual QObject* ownerObject() const = 0;
    virtual QVariantList backgroundPickerImages(bool includeLocalPath = false) const = 0;
    virtual QString resolveBackgroundImageToken(const QString& token) const = 0;
    virtual QString backgroundImageTokenForPath(const QString& path) const = 0;
    virtual bool backgroundImageCacheEnabled() const = 0;
    virtual void watchBackgroundImageChanges(QObject* context,
                                             std::function<void()> callback) = 0;
};
