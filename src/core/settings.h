#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QTimer>
// Domain sub-objects are forward-declared. The QML-facing Q_PROPERTYs return
// QObject* (a known type that QML can introspect) so this header doesn't need
// to include the twelve sub-object headers — preserving the recompile-blast
// reduction this whole refactor is for.
//
// C++ callers use the typed accessor (e.g. `settings->mqtt()`) and include
// `settings_mqtt.h` themselves where they actually dereference.
class SettingsMqtt;
class SettingsAutoWake;
class SettingsHardware;
class SettingsAI;
class SettingsTheme;
class SettingsVisualizer;
class SettingsMcp;
class SettingsBrew;
class SettingsDye;
class SettingsNetwork;
class SettingsApp;
class SettingsCalibration;
class SettingsConnections;

class Settings : public QObject {
    Q_OBJECT

    // Domain sub-objects exposed to QML as QObject* so QML can resolve
    // `Settings.mqtt.mqttEnabled` via the runtime metaObject (SettingsMqtt's
    // Q_OBJECT supplies it). The typed `mqtt()` accessor below is what C++
    // callers use.
    //
    // Required prerequisite: each sub-object type must be registered with the
    // QML engine via qmlRegisterUncreatableType<SettingsXxx>(...) in main.cpp,
    // otherwise QML can't discover the concrete type and resolves the chained
    // property access (e.g. `.customThemeColors`) to `undefined` at runtime.
    Q_PROPERTY(QObject* mqtt READ mqttQObject CONSTANT)
    Q_PROPERTY(QObject* autoWake READ autoWakeQObject CONSTANT)
    Q_PROPERTY(QObject* hardware READ hardwareQObject CONSTANT)
    Q_PROPERTY(QObject* ai READ aiQObject CONSTANT)
    Q_PROPERTY(QObject* theme READ themeQObject CONSTANT)
    Q_PROPERTY(QObject* visualizer READ visualizerQObject CONSTANT)
    Q_PROPERTY(QObject* mcp READ mcpQObject CONSTANT)
    Q_PROPERTY(QObject* brew READ brewQObject CONSTANT)
    Q_PROPERTY(QObject* dye READ dyeQObject CONSTANT)
    Q_PROPERTY(QObject* network READ networkQObject CONSTANT)
    Q_PROPERTY(QObject* app READ appQObject CONSTANT)
    Q_PROPERTY(QObject* calibration READ calibrationQObject CONSTANT)
    Q_PROPERTY(QObject* connections READ connectionsQObject CONSTANT)

    // Machine settings
    Q_PROPERTY(QString machineAddress READ machineAddress WRITE setMachineAddress NOTIFY machineAddressChanged)
    Q_PROPERTY(QString scaleAddress READ scaleAddress WRITE setScaleAddress NOTIFY scaleAddressChanged)
    Q_PROPERTY(QString scaleType READ scaleType WRITE setScaleType NOTIFY scaleTypeChanged)
    Q_PROPERTY(bool keepScaleOn READ keepScaleOn WRITE setKeepScaleOn NOTIFY keepScaleOnChanged)
    Q_PROPERTY(QString scaleName READ scaleName WRITE setScaleName NOTIFY scaleNameChanged)

    // Multi-scale management
    Q_PROPERTY(QVariantList knownScales READ knownScales NOTIFY knownScalesChanged)
    Q_PROPERTY(QString primaryScaleAddress READ primaryScaleAddress NOTIFY knownScalesChanged)

    // FlowScale (virtual scale from flow data)
    Q_PROPERTY(bool useFlowScale READ useFlowScale WRITE setUseFlowScale NOTIFY useFlowScaleChanged)

    // Allow user to disable modal scale connection alert dialogs
    Q_PROPERTY(bool showScaleDialogs READ showScaleDialogs WRITE setShowScaleDialogs NOTIFY showScaleDialogsChanged)

    // Refractometer (DiFluid R2)
    Q_PROPERTY(QString savedRefractometerAddress READ savedRefractometerAddress WRITE setSavedRefractometerAddress NOTIFY savedRefractometerChanged)
    Q_PROPERTY(QString savedRefractometerName READ savedRefractometerName WRITE setSavedRefractometerName NOTIFY savedRefractometerChanged)

    // Enable USB serial polling for DE1 connection. Off by default to save battery
    // (polling every 2 s). Only needed when connecting the DE1 via USB-C cable.
    Q_PROPERTY(bool usbSerialEnabled READ usbSerialEnabled WRITE setUsbSerialEnabled NOTIFY usbSerialEnabledChanged)

public:
    explicit Settings(QObject* parent = nullptr);

    // Domain sub-object accessors (typed, for C++ callers — header forward-declares
    // the types so callers must include the specific settings_<domain>.h to dereference).
    SettingsMqtt* mqtt() const { return m_mqtt; }
    SettingsAutoWake* autoWake() const { return m_autoWake; }
    SettingsHardware* hardware() const { return m_hardware; }
    SettingsAI* ai() const { return m_ai; }
    SettingsTheme* theme() const { return m_theme; }
    SettingsVisualizer* visualizer() const { return m_visualizer; }
    SettingsMcp* mcp() const { return m_mcp; }
    SettingsBrew* brew() const { return m_brew; }
    SettingsDye* dye() const { return m_dye; }
    SettingsNetwork* network() const { return m_network; }
    SettingsApp* app() const { return m_app; }
    SettingsCalibration* calibration() const { return m_calibration; }
    SettingsConnections* connections() const { return m_connections; }

    // QML-facing accessors — implemented out-of-line in settings.cpp where the
    // SettingsXxx -> QObject* upcast is visible. QML uses these via Q_PROPERTY.
    QObject* mqttQObject() const;
    QObject* autoWakeQObject() const;
    QObject* hardwareQObject() const;
    QObject* aiQObject() const;
    QObject* themeQObject() const;
    QObject* visualizerQObject() const;
    QObject* mcpQObject() const;
    QObject* brewQObject() const;
    QObject* dyeQObject() const;
    QObject* networkQObject() const;
    QObject* appQObject() const;
    QObject* calibrationQObject() const;
    QObject* connectionsQObject() const;

    // Machine settings
    QString machineAddress() const;
    void setMachineAddress(const QString& address);

    QString scaleAddress() const;
    void setScaleAddress(const QString& address);

    bool keepScaleOn() const;
    void setKeepScaleOn(bool keep);

    QString scaleType() const;
    void setScaleType(const QString& type);

    QString scaleName() const;
    void setScaleName(const QString& name);

    // Multi-scale management
    Q_INVOKABLE QVariantList knownScales() const;
    Q_INVOKABLE void addKnownScale(const QString& address, const QString& type, const QString& name);
    Q_INVOKABLE void removeKnownScale(const QString& address);
    Q_INVOKABLE void setPrimaryScale(const QString& address);
    Q_INVOKABLE QString primaryScaleAddress() const;
    Q_INVOKABLE bool isKnownScale(const QString& address) const;

    // FlowScale
    bool useFlowScale() const;
    void setUseFlowScale(bool enabled);

    // Scale connection alert dialogs
    bool showScaleDialogs() const;
    void setShowScaleDialogs(bool enabled);

    // Refractometer
    QString savedRefractometerAddress() const;
    void setSavedRefractometerAddress(const QString& address);
    QString savedRefractometerName() const;
    void setSavedRefractometerName(const QString& name);

    // USB serial polling
    bool usbSerialEnabled() const;
    void setUsbSerialEnabled(bool enabled);

    // Force sync to disk
    void sync() { m_settings.sync(); }

    Q_INVOKABLE void factoryReset();

    // Generic settings access (for extensibility)
    Q_INVOKABLE QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);

    // Coerced boolean getter. QSettings' INI backend (used on Android/Linux/iOS)
    // round-trips booleans as the strings "true"/"false", which JavaScript then
    // treats as truthy — so `property bool foo: Settings.value("foo", true)`
    // returned the wrong value after the key had been written once. This helper
    // performs the coercion in C++ so QML callers don't have to.
    Q_INVOKABLE bool boolValue(const QString& key, bool defaultValue = false) const;

signals:
    void machineAddressChanged();
    void scaleAddressChanged();
    void scaleTypeChanged();
    void keepScaleOnChanged();
    void scaleNameChanged();
    void knownScalesChanged();
    void useFlowScaleChanged();
    void showScaleDialogsChanged();
    void savedRefractometerChanged();
    void usbSerialEnabledChanged();
    void valueChanged(const QString& key);

private:
    void writeKnownScales(const QVariantList& scales);

    mutable QSettings m_settings;

    // Domain sub-objects (composition façade)
    SettingsMqtt* m_mqtt = nullptr;
    SettingsAutoWake* m_autoWake = nullptr;
    SettingsHardware* m_hardware = nullptr;
    SettingsAI* m_ai = nullptr;
    SettingsTheme* m_theme = nullptr;
    SettingsVisualizer* m_visualizer = nullptr;
    SettingsMcp* m_mcp = nullptr;
    SettingsBrew* m_brew = nullptr;
    SettingsDye* m_dye = nullptr;
    SettingsNetwork* m_network = nullptr;
    SettingsApp* m_app = nullptr;
    SettingsCalibration* m_calibration = nullptr;
    SettingsConnections* m_connections = nullptr;
};
