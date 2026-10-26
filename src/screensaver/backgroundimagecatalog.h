#pragma once

#include <QVariantList>
#include <QString>
#include <functional>

class QObject;

// Narrow host-owned catalogue contract shared by theme settings and the
// screensaver manager. Public rows carry opaque IDs and presentation metadata,
// including portable references for official images. Local paths are included
// only when the native in-process caller explicitly requests them.
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

    QString providedImageReference(const QString& path) const {
        for (const QVariant& value : backgroundPickerImages(true)) {
            const auto row = value.toMap();
            if (!row.value("personal").toBool() && row.value("path").toString() == path)
                return row.value("reference").toString();
        }
        return {};
    }

    QString resolveProvidedImageReference(const QString& reference) const {
        if (reference.isEmpty()) return {};
        for (const QVariant& value : backgroundPickerImages(true)) {
            const auto row = value.toMap();
            if (!row.value("personal").toBool() && row.value("reference").toString() == reference)
                return row.value("path").toString();
        }
        return {};
    }
};
