#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Read-only view of a normalized SettingsNetwork layout. Kept pure so native
// SettingsNetwork and the shared QtRO facade use one interpretation.
class LayoutSnapshot final
{
public:
    explicit LayoutSnapshot(QJsonObject layout = {});
    explicit LayoutSnapshot(const QString& compactJson);

    const QJsonObject& object() const { return m_layout; }
    QVariantList zoneItems(const QString& zoneName) const;
    int zoneYOffset(const QString& zoneName) const;
    double zoneScale(const QString& zoneName) const;
    QString zoneOption(const QString& zoneName, const QString& key,
                       const QString& defaultValue) const;
    QVariantMap itemProperties(const QString& itemId) const;
    bool itemIsConfigured(const QString& itemId) const;

private:
    QJsonObject m_layout;
};
