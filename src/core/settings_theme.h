#pragma once

#include <QMap>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>

// Theme, color palette, font, shader, and flash settings.
// Owns its own QSettings instance — same backing store as Settings.
class SettingsTheme : public QObject {
    Q_OBJECT

    // Skin (asset folder selection)
    Q_PROPERTY(QString skin READ skin WRITE setSkin NOTIFY skinChanged)
    Q_PROPERTY(QString skinPath READ skinPath NOTIFY skinChanged)

    // Theme palette
    Q_PROPERTY(QVariantMap customThemeColors READ customThemeColors WRITE setCustomThemeColors NOTIFY customThemeColorsChanged)
    Q_PROPERTY(QVariantList colorGroups READ colorGroups WRITE setColorGroups NOTIFY colorGroupsChanged)
    Q_PROPERTY(QString activeThemeName READ activeThemeName WRITE setActiveThemeName NOTIFY activeThemeNameChanged)
    Q_PROPERTY(QString darkThemeName READ darkThemeName WRITE setDarkThemeName NOTIFY darkThemeNameChanged)
    Q_PROPERTY(QString lightThemeName READ lightThemeName WRITE setLightThemeName NOTIFY lightThemeNameChanged)
    Q_PROPERTY(QStringList themeNames READ themeNames NOTIFY themeNamesChanged)
    Q_PROPERTY(double screenBrightness READ screenBrightness WRITE setScreenBrightness NOTIFY screenBrightnessChanged)
    Q_PROPERTY(QVariantMap customFontSizes READ customFontSizes WRITE setCustomFontSizes NOTIFY customFontSizesChanged)
    // Defaults merged with the user's overrides — the single value QML should render at.
    // A PROPERTY, not an invokable: a binding re-evaluates when a NOTIFY fires for a
    // property it READ during its last evaluation. A Q_INVOKABLE call registers no such
    // dependency, so a binding over fontSizeFor("labelSize") would never re-run on its own
    // when a slider moves. (An invokable CAN work if the same expression also reads a
    // notifying property — see Tr.qml, which reads translationVersion for exactly that
    // reason — but relying on that is a trap, so the value is exposed as a property.)
    Q_PROPERTY(QVariantMap effectiveFontSizes READ effectiveFontSizes NOTIFY customFontSizesChanged)

    // The bundled UI font family main.cpp actually registered, or empty if registration
    // failed. CONSTANT: main.cpp sets it before the QML engine is created and it never
    // changes, so no notify is needed. Exists so Theme.qml can state the family on every
    // font role explicitly instead of relying on application-font inheritance (#1537).
    Q_PROPERTY(QString bundledFontFamily READ bundledFontFamily CONSTANT)

    // Theme mode (light/dark/system)
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool isDarkMode READ isDarkMode NOTIFY isDarkModeChanged)
    Q_PROPERTY(QString editingPalette READ editingPalette WRITE setEditingPalette NOTIFY editingPaletteChanged)

    // Custom background image, applied app-wide (see add-custom-background).
    // Absolute filesystem path; empty = today's flat Theme.backgroundColor. Same image in
    // both light and dark mode. Sourced from the screensaver media library (personal
    // uploads + locally-cached catalog images) — see ScreensaverVideoManager.
    Q_PROPERTY(QString backgroundImagePath READ backgroundImagePath WRITE setBackgroundImagePath NOTIFY backgroundImagePathChanged)

    // Screen shaders
    Q_PROPERTY(QString activeShader READ activeShader WRITE setActiveShader NOTIFY activeShaderChanged)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams NOTIFY shaderParamsChanged)

    // Theme flash (in-memory only, for identifying colors on device)
    Q_PROPERTY(QString flashColorName READ flashColorName NOTIFY flashColorNameChanged)
    Q_PROPERTY(int flashPhase READ flashPhase NOTIFY flashPhaseChanged)

    // Colors detected on the current page (set from QML tree walker)
    Q_PROPERTY(QStringList currentPageColors READ currentPageColors WRITE setCurrentPageColors NOTIFY currentPageColorsChanged)

public:
    explicit SettingsTheme(QObject* parent = nullptr);

    QString skin() const;
    void setSkin(const QString& skin);
    QString skinPath() const;

    QVariantMap customThemeColors() const;
    void setCustomThemeColors(const QVariantMap& colors);

    QVariantList colorGroups() const;
    void setColorGroups(const QVariantList& groups);

    QString activeThemeName() const;
    void setActiveThemeName(const QString& name);

    QString darkThemeName() const;
    void setDarkThemeName(const QString& name);
    QString lightThemeName() const;
    void setLightThemeName(const QString& name);
    QStringList themeNames() const;
    Q_INVOKABLE void applyDarkTheme(const QString& name);
    Q_INVOKABLE void applyLightTheme(const QString& name);

    QString themeMode() const;
    void setThemeMode(const QString& mode);
    bool isDarkMode() const { return m_isDarkMode; }
    void initSystemThemeDetection();

    QString editingPalette() const { return m_editingPalette; }
    void setEditingPalette(const QString& palette);

    QString backgroundImagePath() const;
    void setBackgroundImagePath(const QString& path);
    Q_INVOKABLE QVariantMap editingPaletteColors() const;
    Q_INVOKABLE void setEditingPaletteColor(const QString& colorName, const QString& colorValue);

    static const QVariantMap& darkDefaults();
    static const QVariantMap& lightDefaults();

    QString activeShader() const;
    void setActiveShader(const QString& shader);
    QVariantMap shaderParams() const;
    Q_INVOKABLE void setShaderParam(const QString& name, double value);
    Q_INVOKABLE QVariantMap effectParams(const QString& effectId) const;
    Q_INVOKABLE void setEffectParam(const QString& effectId, const QString& name, double value);
    QJsonObject screenEffectJson() const;
    void applyScreenEffect(const QJsonObject& screenEffect);

    Q_INVOKABLE void setThemeColor(const QString& colorName, const QString& colorValue);
    Q_INVOKABLE QString getThemeColor(const QString& colorName) const;
    Q_INVOKABLE void resetThemeToDefault();
    Q_INVOKABLE QVariantList getPresetThemes() const;
    Q_INVOKABLE void applyPresetTheme(const QString& name);
    Q_INVOKABLE void saveCurrentTheme(const QString& name);
    Q_INVOKABLE void deleteUserTheme(const QString& name);
    Q_INVOKABLE bool saveThemeToFile(const QString& filePath);
    Q_INVOKABLE bool loadThemeFromFile(const QString& filePath);
    Q_INVOKABLE QVariantMap generatePalette(double hue, double saturation, double lightness) const;

    // A font size role: its default and the range a user may set it to. Bounds live here
    // rather than only in the web editor's sliders, so they are enforced wherever a value
    // can be written — the editor's JS min/max is a UI affordance, not a validation.
    struct FontRole {
        int def;
        int min;
        int max;
    };

    // Canonical font-size roles. The ONLY declaration — QML theme roles, the web theme
    // editor, its slider bounds, and startup override logging all read through here, so
    // the value the UI renders at and the value the editor reports cannot drift apart.
    static const QMap<QString, FontRole>& fontRoles();
    // Convenience view for callers that only need the defaults.
    static const QMap<QString, int>& fontSizeDefaults();

    // Set once from main.cpp immediately after font registration, before the QML engine
    // exists. Static because SettingsTheme is not constructed yet at that point.
    static void setBundledFontFamily(const QString& family);
    static QString bundledFontFamily();

    QVariantMap customFontSizes() const;
    QVariantMap effectiveFontSizes() const;
    // Only the roles whose effective value differs from the default. A stored value that
    // happens to equal the default (user moved a slider and moved it back) is not an
    // override and is not reported.
    QVariantMap fontSizeOverrides() const;
    void setCustomFontSizes(const QVariantMap& sizes);
    Q_INVOKABLE void setFontSize(const QString& fontName, int size);
    Q_INVOKABLE int getFontSize(const QString& fontName) const;
    Q_INVOKABLE void resetFontSizesToDefault();

    QString flashColorName() const { return m_flashColorName; }
    int flashPhase() const { return m_flashPhase; }
    Q_INVOKABLE void flashThemeColor(const QString& colorName);

    QStringList currentPageColors() const { return m_currentPageColors; }
    void setCurrentPageColors(const QStringList& colors);

    double screenBrightness() const;
    void setScreenBrightness(double brightness);

signals:
    void skinChanged();
    void customThemeColorsChanged();
    void colorGroupsChanged();
    void activeThemeNameChanged();
    void darkThemeNameChanged();
    void lightThemeNameChanged();
    void themeNamesChanged();
    void themeModeChanged();
    void isDarkModeChanged();
    void editingPaletteChanged();
    void backgroundImagePathChanged();
    void activeShaderChanged();
    void shaderParamsChanged();
    void customFontSizesChanged();
    void flashColorNameChanged();
    void flashPhaseChanged();
    void currentPageColorsChanged();
    void screenBrightnessChanged();

private:
    void updateResolvedMode();

    mutable QSettings m_settings;
    bool m_isDarkMode = true;
    QString m_editingPalette = "dark";

    QString m_flashColorName;
    int m_flashPhase = 0;
    QTimer* m_flashTimer = nullptr;

    QStringList m_currentPageColors;
};
