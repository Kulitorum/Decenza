#include "remotesettings.h"

#include "remotesessionbackend.h"
#include "core/backgroundpresets.h"
#include "core/themefonts.h"

#include <QColor>
#include <QJSEngine>
#include <QQmlEngine>
#include <QUuid>

#include <type_traits>

static_assert(!std::is_default_constructible_v<RemoteSettings>,
              "Settings must only be created through its QML engine factory");

namespace {
QVariantMap themeInputs(const QVariantMap& appearance)
{
    return appearance.value(QStringLiteral("themeInputs")).toMap();
}
}

RemoteSettingsTheme::RemoteSettingsTheme(QObject* parent)
    : QObject(parent)
{
}

bool RemoteSettingsTheme::isDarkMode() const
{
    return m_appearance.value(QStringLiteral("darkMode")).toBool();
}

QVariantMap RemoteSettingsTheme::customThemeColors() const
{
    return themeInputs(m_appearance).value(QStringLiteral("customThemeColors")).toMap();
}

QVariantMap RemoteSettingsTheme::effectiveFontSizes() const
{
    const QVariantMap hostSizes = m_appearance.value(QStringLiteral("fontSizes")).toMap();
    return hostSizes.isEmpty() ? ThemeFonts::sizeDefaultsAsVariantMap() : hostSizes;
}

QString RemoteSettingsTheme::backgroundPreset() const
{
    return themeInputs(m_appearance).value(QStringLiteral("backgroundPreset")).toString();
}

QString RemoteSettingsTheme::backgroundSource() const
{
    return m_appearance.value(QStringLiteral("background")).toMap()
        .value(QStringLiteral("source"), QStringLiteral("none")).toString();
}

QString RemoteSettingsTheme::backgroundPattern() const
{
    return m_appearance.value(QStringLiteral("background")).toMap()
        .value(QStringLiteral("patternId")).toString();
}

QUrl RemoteSettingsTheme::backgroundImageUrl() const
{
    if (backgroundSource() != QStringLiteral("image") || m_apiOrigin.isEmpty()) return {};
    const QString revision = m_appearance.value(QStringLiteral("background")).toMap()
        .value(QStringLiteral("imageRevision")).toString();
    return QUrl(m_apiOrigin + QStringLiteral("/api/session/background-image/") + revision);
}

bool RemoteSettingsTheme::backgroundShotAdvanced() const
{
    return themeInputs(m_appearance).value(QStringLiteral("backgroundShotAdvanced")).toBool();
}

QVariantList RemoteSettingsTheme::backgroundPresets() const
{
    return BackgroundPresets::coloursAsVariantList();
}

QVariantList RemoteSettingsTheme::backgroundPatterns() const
{
    return BackgroundPresets::patternsAsVariantList();
}

QVariantMap RemoteSettingsTheme::activeBackgroundPreset() const
{
    const BackgroundPresets::Colour colour = BackgroundPresets::colourById(backgroundPreset());
    return colour.id.isEmpty() ? QVariantMap() : BackgroundPresets::colourToVariantMap(colour);
}

QVariantMap RemoteSettingsTheme::derivedBackgroundColors() const
{
    const QString id = backgroundPreset();
    if (id.isEmpty())
        return {};
    const BackgroundPresets::Colour colour = BackgroundPresets::colourById(id);
    return colour.id.isEmpty() ? QVariantMap()
                               : BackgroundPresets::deriveAsVariantMap(QColor(colour.value));
}

QVariantMap RemoteSettingsTheme::deriveColorsFor(const QString& colourId) const
{
    const BackgroundPresets::Colour colour = BackgroundPresets::colourById(colourId);
    return colour.id.isEmpty() ? QVariantMap()
                               : BackgroundPresets::deriveAsVariantMap(QColor(colour.value));
}

bool RemoteSettingsTheme::glassChrome() const
{
    return themeInputs(m_appearance).value(QStringLiteral("glassChrome")).toBool();
}

QString RemoteSettingsTheme::flashColorName() const
{
    return themeInputs(m_appearance).value(QStringLiteral("flashColorName")).toString();
}

int RemoteSettingsTheme::flashPhase() const
{
    return themeInputs(m_appearance).value(QStringLiteral("flashPhase")).toInt();
}

QString RemoteSettingsTheme::bundledFontFamily() const
{
    return themeInputs(m_appearance).value(QStringLiteral("bundledFontFamily")).toString();
}

QString RemoteSettingsTheme::symbolFontFamily() const
{
    return themeInputs(m_appearance).value(QStringLiteral("symbolFontFamily")).toString();
}

QString RemoteSettingsTheme::activeShader() const
{
    return themeInputs(m_appearance).value(QStringLiteral("activeShader")).toString();
}

QVariantMap RemoteSettingsTheme::shaderParams() const
{
    return themeInputs(m_appearance).value(QStringLiteral("shaderParams")).toMap();
}

QString RemoteSettingsTheme::adjustedForContrast(const QString& foreground,
                                                 const QString& background) const
{
    const QColor fg(foreground);
    const QColor bg(background);
    if (!fg.isValid() || !bg.isValid())
        return foreground;
    return BackgroundPresets::adjustForContrast(
               fg, BackgroundPresets::pageUnderDensestPattern(bg)).name();
}

void RemoteSettingsTheme::setAppearance(const QVariantMap& appearance, const QString& apiOrigin)
{
    if (m_appearance == appearance && m_apiOrigin == apiOrigin)
        return;
    m_apiOrigin = apiOrigin;
    m_appearance = appearance;
    emit changed();
}

void RemoteSettingsGraph::setSnapshot(const QVariantMap& values)
{
    if (m_values == values) return;
    m_values = values;
    emit changed();
}

RemoteSettingsApp::RemoteSettingsApp(QObject* parent)
    : RemoteMachineUiSource(parent)
{
    connect(this, &RemoteMachineUiSource::machineUiChanged,
            this, &RemoteSettingsApp::changed);
}

QString RemoteSettingsApp::temperatureUnit() const
{
    return m_appearance.value(QStringLiteral("fahrenheit")).toBool()
        ? QStringLiteral("fahrenheit") : QStringLiteral("celsius");
}

bool RemoteSettingsApp::use12HourTime() const
{
    return themeInputs(m_appearance).value(QStringLiteral("use12HourTime")).toBool();
}

QString RemoteSettingsApp::waterLevelDisplayUnit() const
{
    return machineUi().appWaterLevelDisplayUnit;
}

int RemoteSettingsApp::waterRefillPoint() const
{
    return machineUi().appWaterRefillPoint;
}

int RemoteSettingsApp::refillKitOverride() const
{
    return machineUi().appRefillKitOverride;
}

void RemoteSettingsApp::setAppearance(const QVariantMap& appearance)
{
    if (m_appearance == appearance)
        return;
    m_appearance = appearance;
    emit changed();
}

RemoteSettingsBrew::RemoteSettingsBrew(QObject* parent)
    : RemoteMachineUiSource(parent)
{
    connect(this, &RemoteMachineUiSource::machineUiChanged,
            this, &RemoteSettingsBrew::changed);
}

bool RemoteSettingsBrew::hasTemperatureOverride() const
{
    return machineUi().brewHasTemperatureOverride;
}

double RemoteSettingsBrew::temperatureOverride() const
{
    return machineUi().brewTemperatureOverride;
}

double RemoteSettingsBrew::doseCupTareWeight() const
{
    return machineUi().brewDoseCupTareWeight;
}

RemoteSettingsDye::RemoteSettingsDye(QObject* parent)
    : QObject(parent)
{
}

qint64 RemoteSettingsDye::activeEquipmentId() const
{
    return m_equipment && m_equipment->ready() ? m_equipment->activePackageId() : -1;
}

void RemoteSettingsDye::setEquipment(EquipmentService* equipment)
{
    if (m_equipment == equipment)
        return;
    const qint64 previousId = activeEquipmentId();
    if (m_destroyedConnection)
        disconnect(m_destroyedConnection);
    if (m_stateConnection)
        disconnect(m_stateConnection);

    m_equipment = equipment;
    if (m_equipment) {
        m_stateConnection = connect(m_equipment, &EquipmentService::stateChanged,
                                    this, &RemoteSettingsDye::updateActiveEquipmentId);
        m_destroyedConnection = connect(m_equipment, &QObject::destroyed, this, [this] {
            m_equipment = nullptr;
            m_destroyedConnection = {};
            m_stateConnection = {};
            updateActiveEquipmentId();
            emit equipmentChanged();
        });
    }
    m_lastActiveEquipmentId = activeEquipmentId();
    emit equipmentChanged();
    if (previousId != m_lastActiveEquipmentId)
        emit activeEquipmentIdChanged();
}

void RemoteSettingsDye::switchToEquipment(const QVariantMap& package)
{
    if (!m_equipment)
        return;
    const qint64 id = package.value(QStringLiteral("id")).toLongLong();
    if (id <= 0)
        return;

    // The native service applies the selected package and publishes the new
    // active id; this facade does not optimistically update local settings.
    m_equipment->activatePackage(id, true,
        QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void RemoteSettingsDye::updateActiveEquipmentId()
{
    const qint64 id = activeEquipmentId();
    if (id == m_lastActiveEquipmentId)
        return;
    m_lastActiveEquipmentId = id;
    emit activeEquipmentIdChanged();
}

RemoteSettings::RemoteSettings(QObject* parent)
    : RemoteMachineUiSource(parent)
    , m_theme(new RemoteSettingsTheme(this))
    , m_app(new RemoteSettingsApp(this))
    , m_hardware(new RemoteSettingsHardware(this))
    , m_brew(new RemoteSettingsBrew(this))
    , m_graph(new RemoteSettingsGraph(this))
    , m_dye(new RemoteSettingsDye(this))
{
}

RemoteSettings* RemoteSettings::create(QQmlEngine* engine, QJSEngine*)
{
    auto* settings = new RemoteSettings(engine);
    QJSEngine::setObjectOwnership(settings, QJSEngine::CppOwnership);
    return settings;
}

void RemoteSettings::setSession(RemoteSessionBackend* session)
{
    if (this->session() == session)
        return;
    disconnect(m_appearanceConnection);
    disconnect(m_destroyedConnection);
    RemoteMachineUiSource::setSession(session);
    m_app->setSession(session);
    m_hardware->setSession(session);
    m_brew->setSession(session);
    if (session) {
        m_appearanceConnection = connect(session, &RemoteSessionBackend::appearanceChanged,
                                         this, &RemoteSettings::applyAppearance);
        m_destroyedConnection = connect(session, &QObject::destroyed, this, [this] {
            m_appearanceConnection = {};
            m_destroyedConnection = {};
            applyAppearance();
        });
    }
    applyAppearance();
}

void RemoteSettings::applyAppearance()
{
    const QVariantMap appearance = session() ? session()->appearance() : QVariantMap();
    m_theme->setAppearance(appearance, session() ? session()->apiOrigin() : QString());
    m_graph->setSnapshot(appearance.value(QStringLiteral("graph")).toMap());
    m_app->setAppearance(appearance);
}
