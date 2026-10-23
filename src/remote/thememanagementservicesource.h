#pragma once

#include "rep_thememanagementservice_source.h"
#include "serviceoperation.h"
#include "../screensaver/backgroundimagecatalog.h"

#include <QPointer>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>

class Settings;
class SettingsTheme;

class NativeThemeManagementSource final : public ThemeManagementStateSimpleSource
{
    Q_OBJECT
public:
    explicit NativeThemeManagementSource(Settings* settings,
                                         BackgroundImageCatalog* screensaver = nullptr,
                                         QObject* parent = nullptr);

    void refresh() override;
    void execute(QString requestId, QString operation, QString name,
                 qint64 expectedRevision) override;
    void setPaletteColor(QString requestId, QString palette, QString colorName,
                         QString colorValue, qint64 expectedRevision) override;
    void executeSetting(QString requestId, QString operation, QVariantMap arguments,
                        qint64 expectedRevision) override;

private:
    void publishSnapshot(bool advanceRevision);
    void finish(const QString& requestId, const QString& operation, bool success,
                const QString& reason = {});
    bool validName(const QString& name) const;
    bool isUserTheme(const QString& name) const;
    bool isBuiltIn(const QString& name) const;

    QPointer<SettingsTheme> m_theme;
    BackgroundImageCatalog* m_screensaver = nullptr;
    QPointer<QObject> m_screensaverOwner;
    ServiceRequestIds m_requests;
    QVariantList m_presets;
    QVariantMap m_activeColors;
    QVariantMap m_palettes;
    QString m_activeThemeName;
    bool m_darkMode = false;
    bool m_suppressChangeSignals = false;
    qint64 m_revision = 0;
    QString m_themeMode;
    QStringList m_themeNames;
    QString m_darkThemeName;
    QString m_lightThemeName;
    bool m_glassChrome = false;
    QString m_backgroundPreset;
    QString m_backgroundSource;
    QString m_backgroundPattern;
    bool m_backgroundShotAdvanced = false;
    QVariantList m_backgroundImages;
    bool m_backgroundImageCacheEnabled = true;
    QString m_backgroundImageId;
    QVariantList m_backgroundPresets;
    QVariantList m_backgroundPatterns;
};
