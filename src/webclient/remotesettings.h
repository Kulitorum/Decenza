#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <type_traits>
#include <QtQmlIntegration/qqmlintegration.h>
#include "remotesessionbackend.h"
#include "remotemachineuisource.h"
#include "remote/equipmentservice.h"
#include "core/graphpreferences.h"

class QQmlEngine;
class QJSEngine;

class RemoteSettingsTheme final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("RemoteSettingsTheme is owned by Settings")

    Q_PROPERTY(bool isDarkMode READ isDarkMode NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap customThemeColors READ customThemeColors NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap effectiveFontSizes READ effectiveFontSizes NOTIFY changed FINAL)
    Q_PROPERTY(QString backgroundPreset READ backgroundPreset NOTIFY changed FINAL)
    Q_PROPERTY(QString backgroundSource READ backgroundSource NOTIFY changed FINAL)
    Q_PROPERTY(QString backgroundPattern READ backgroundPattern NOTIFY changed FINAL)
    Q_PROPERTY(QUrl backgroundImageUrl READ backgroundImageUrl NOTIFY changed FINAL)
    Q_PROPERTY(bool backgroundShotAdvanced READ backgroundShotAdvanced NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList backgroundPresets READ backgroundPresets CONSTANT FINAL)
    Q_PROPERTY(QVariantList backgroundPatterns READ backgroundPatterns CONSTANT FINAL)
    Q_PROPERTY(QVariantMap activeBackgroundPreset READ activeBackgroundPreset NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap derivedBackgroundColors READ derivedBackgroundColors NOTIFY changed FINAL)
    Q_PROPERTY(bool glassChrome READ glassChrome NOTIFY changed FINAL)
    Q_PROPERTY(QString flashColorName READ flashColorName NOTIFY changed FINAL)
    Q_PROPERTY(int flashPhase READ flashPhase NOTIFY changed FINAL)
    Q_PROPERTY(QString bundledFontFamily READ bundledFontFamily NOTIFY changed FINAL)
    Q_PROPERTY(QString symbolFontFamily READ symbolFontFamily NOTIFY changed FINAL)
    Q_PROPERTY(QString activeShader READ activeShader NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap shaderParams READ shaderParams NOTIFY changed FINAL)

public:
    explicit RemoteSettingsTheme(QObject* parent = nullptr);

    bool isDarkMode() const;
    QVariantMap customThemeColors() const;
    QVariantMap effectiveFontSizes() const;
    QString backgroundPreset() const;
    QString backgroundSource() const;
    QString backgroundPattern() const;
    QUrl backgroundImageUrl() const;
    bool backgroundShotAdvanced() const;
    QVariantList backgroundPresets() const;
    QVariantList backgroundPatterns() const;
    QVariantMap activeBackgroundPreset() const;
    QVariantMap derivedBackgroundColors() const;
    bool glassChrome() const;
    QString flashColorName() const;
    int flashPhase() const;
    QString bundledFontFamily() const;
    QString symbolFontFamily() const;
    QString activeShader() const;
    QVariantMap shaderParams() const;

    Q_INVOKABLE QString adjustedForContrast(const QString& foreground,
                                            const QString& background) const;
    Q_INVOKABLE QVariantMap deriveColorsFor(const QString& colourId) const;

    void setAppearance(const QVariantMap& appearance, const QString& apiOrigin = {});

signals:
    void changed();

private:
    QVariantMap m_appearance;
    QString m_apiOrigin;
};

class RemoteSettingsGraph final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("RemoteSettingsGraph is owned by Settings")
#define GRAPH_PROPERTY(Getter, Setter, Key, Default, Signal) Q_PROPERTY(bool Getter READ Getter NOTIFY changed FINAL)
    DECENZA_GRAPH_TRACES(GRAPH_PROPERTY)
#undef GRAPH_PROPERTY
    Q_PROPERTY(bool advancedMode READ advancedMode NOTIFY changed FINAL)
    Q_PROPERTY(int flowMultiplier READ flowMultiplier NOTIFY changed FINAL)
    Q_PROPERTY(QString rightAxisMode READ rightAxisMode NOTIFY changed FINAL)
public:
    explicit RemoteSettingsGraph(QObject* parent = nullptr) : QObject(parent) {}
#define GRAPH_GETTER(Getter, Setter, Key, Default, Signal) bool Getter() const { return m_values.value(QStringLiteral(#Getter), Default).toBool(); }
    DECENZA_GRAPH_TRACES(GRAPH_GETTER)
#undef GRAPH_GETTER
    bool advancedMode() const { return m_values.value(QStringLiteral("advancedMode")).toBool(); }
    int flowMultiplier() const { return m_values.value(QStringLiteral("flowMultiplier"), 2).toInt(); }
    QString rightAxisMode() const { return m_values.value(QStringLiteral("rightAxisMode"), QStringLiteral("weight")).toString(); }
    void setSnapshot(const QVariantMap& values);
signals:
    void changed();
private:
    QVariantMap m_values;
};

class RemoteSettingsHardware final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("RemoteSettingsHardware is owned by Settings")
    Q_PROPERTY(bool steamTwoTapStop READ steamTwoTapStop NOTIFY machineUiChanged FINAL)
public:
    explicit RemoteSettingsHardware(QObject* parent = nullptr) : RemoteMachineUiSource(parent) {}
    bool steamTwoTapStop() const { return machineUi().hardwareSteamTwoTapStop; }
};

class RemoteSettingsApp final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("RemoteSettingsApp is owned by Settings")

    Q_PROPERTY(QString temperatureUnit READ temperatureUnit NOTIFY changed FINAL)
    Q_PROPERTY(bool use12HourTime READ use12HourTime NOTIFY changed FINAL)
    Q_PROPERTY(bool hasQuick3D READ hasQuick3D CONSTANT FINAL)
    Q_PROPERTY(QString waterLevelDisplayUnit READ waterLevelDisplayUnit NOTIFY changed FINAL)
    Q_PROPERTY(int waterRefillPoint READ waterRefillPoint NOTIFY changed FINAL)
    Q_PROPERTY(int refillKitOverride READ refillKitOverride NOTIFY changed FINAL)

public:
    explicit RemoteSettingsApp(QObject* parent = nullptr);
    QString temperatureUnit() const;
    bool use12HourTime() const;
    bool hasQuick3D() const { return true; }
    QString waterLevelDisplayUnit() const;
    int waterRefillPoint() const;
    int refillKitOverride() const;
    void setAppearance(const QVariantMap& appearance);

signals:
    void changed();

private:
    QVariantMap m_appearance;
};

// Read-only mirror of the host's active brew preferences used by shared status
// readouts. Values are sampled with the same fresh session snapshot as MachineState.
class RemoteSettingsBrew final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("RemoteSettingsBrew is owned by Settings")

    Q_PROPERTY(bool hasTemperatureOverride READ hasTemperatureOverride NOTIFY changed FINAL)
    Q_PROPERTY(double temperatureOverride READ temperatureOverride NOTIFY changed FINAL)
    Q_PROPERTY(double steamTemperature READ steamTemperature NOTIFY changed FINAL)
    Q_PROPERTY(int steamTimeout READ steamTimeout NOTIFY changed FINAL)
    Q_PROPERTY(int steamFlow READ steamFlow NOTIFY changed FINAL)
    Q_PROPERTY(int steamAutoFlushSeconds READ steamAutoFlushSeconds NOTIFY changed FINAL)
    Q_PROPERTY(bool milkAutoCaptureEnabled READ milkAutoCaptureEnabled NOTIFY changed FINAL)
    Q_PROPERTY(double steamSecondsPerGram READ steamSecondsPerGram NOTIFY changed FINAL)
    Q_PROPERTY(double lastSteamMilkG READ lastSteamMilkG NOTIFY changed FINAL)
    Q_PROPERTY(double lastSteamTimeS READ lastSteamTimeS NOTIFY changed FINAL)
    Q_PROPERTY(double doseCupTareWeight READ doseCupTareWeight NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList steamPitcherPresets READ steamPitcherPresets NOTIFY steamPitcherPresetsChanged FINAL)
    Q_PROPERTY(int selectedSteamPitcher READ selectedSteamPitcher NOTIFY selectedSteamPitcherChanged FINAL)
    Q_PROPERTY(int selectedSteamPitcherDisplayIndex READ selectedSteamPitcherDisplayIndex NOTIFY selectedSteamPitcherChanged FINAL)

public:
    explicit RemoteSettingsBrew(QObject* parent = nullptr);
    bool hasTemperatureOverride() const;
    double temperatureOverride() const;
    double steamTemperature() const;
    int steamTimeout() const;
    int steamFlow() const;
    int steamAutoFlushSeconds() const;
    bool milkAutoCaptureEnabled() const;
    double steamSecondsPerGram() const;
    double lastSteamMilkG() const;
    double lastSteamTimeS() const;
    double doseCupTareWeight() const;
    QVariantList steamPitcherPresets() const;
    int selectedSteamPitcher() const;
    int selectedSteamPitcherDisplayIndex() const;
    Q_INVOKABLE QVariantMap getSteamPitcherPreset(int index) const;
    Q_INVOKABLE int steamPitcherCount() const;
    Q_INVOKABLE bool steamPitcherNameTaken(const QString& name, int ignoreIndex = -1) const;
    Q_INVOKABLE double netMilkForPitcher(int index, double scaleReading) const;
    Q_INVOKABLE int scaledSteamTime(int index, double milkG) const;
    Q_INVOKABLE int effectiveSteamDurationSec(int index, double milkG) const;

signals:
    void changed();
    void steamPitcherPresetsChanged();
    void selectedSteamPitcherChanged();

private:
    void notifySnapshotChanges();
    QVariantList m_notifiedPresets;
    int m_notifiedSelection = -1;
    int m_notifiedDisplayIndex = -1;
};

// Shared native QML can continue reading Settings.dye.activeEquipmentId while
// the host-owned EquipmentService performs activation on the paired QtRO link.
class RemoteSettingsDye final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("RemoteSettingsDye is owned by Settings")

    Q_PROPERTY(EquipmentService* equipment READ equipment WRITE setEquipment NOTIFY equipmentChanged FINAL)
    Q_PROPERTY(qint64 activeEquipmentId READ activeEquipmentId NOTIFY activeEquipmentIdChanged FINAL)

public:
    explicit RemoteSettingsDye(QObject* parent = nullptr);

    EquipmentService* equipment() const { return m_equipment.data(); }
    void setEquipment(EquipmentService* equipment);
    qint64 activeEquipmentId() const;
    Q_INVOKABLE void switchToEquipment(const QVariantMap& package);

signals:
    void equipmentChanged();
    void activeEquipmentIdChanged();

private:
    void updateActiveEquipmentId();

    QPointer<EquipmentService> m_equipment;
    QMetaObject::Connection m_destroyedConnection;
    QMetaObject::Connection m_stateConnection;
    qint64 m_lastActiveEquipmentId = -1;
};

// Native Settings names consumed by the shared theme, forms and layout QML.
// The QML engine owns this facade; Main.qml binds it to the host session.
class RemoteSettings final : public RemoteMachineUiSource
{
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(Settings)

    Q_PROPERTY(RemoteSettingsTheme* theme READ theme CONSTANT FINAL)
    Q_PROPERTY(RemoteSettingsApp* app READ app CONSTANT FINAL)
    Q_PROPERTY(RemoteSettingsHardware* hardware READ hardware CONSTANT FINAL)
    Q_PROPERTY(RemoteSettingsBrew* brew READ brew CONSTANT FINAL)
    Q_PROPERTY(RemoteSettingsGraph* graph READ graph CONSTANT FINAL)
    Q_PROPERTY(RemoteSettingsDye* dye READ dye CONSTANT FINAL)
    Q_PROPERTY(QString primaryScaleAddress READ primaryScaleAddress NOTIFY machineUiChanged FINAL)

public:
    static RemoteSettings* create(QQmlEngine* engine, QJSEngine* jsEngine);

    RemoteSettingsTheme* theme() const { return m_theme; }
    RemoteSettingsApp* app() const { return m_app; }
    RemoteSettingsHardware* hardware() const { return m_hardware; }
    RemoteSettingsBrew* brew() const { return m_brew; }
    RemoteSettingsGraph* graph() const { return m_graph; }
    RemoteSettingsDye* dye() const { return m_dye; }
    QString primaryScaleAddress() const { return machineUi().primaryScaleAddress; }
    void setSession(RemoteSessionBackend* session) override;

private:
    explicit RemoteSettings(QObject* parent = nullptr);
    void applyAppearance();

    RemoteSettingsTheme* m_theme = nullptr;
    RemoteSettingsApp* m_app = nullptr;
    RemoteSettingsHardware* m_hardware = nullptr;
    RemoteSettingsBrew* m_brew = nullptr;
    RemoteSettingsGraph* m_graph = nullptr;
    RemoteSettingsDye* m_dye = nullptr;
    QMetaObject::Connection m_appearanceConnection;
    QMetaObject::Connection m_destroyedConnection;
};

static_assert(!std::is_default_constructible_v<RemoteSettings>,
              "RemoteSettings is a QML_SINGLETON with a create() factory: Qt must not "
              "default-construct a separate settings facade.");
