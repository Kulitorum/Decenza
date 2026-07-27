#pragma once

#include <QObject>
#include "appsettings.h"
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QTimer>
#include <algorithm>
#ifdef DECENZA_TESTING
#include <QCoreApplication>
#include <QDir>
#endif
// The twelve domain sub-objects are INCLUDED, not forward-declared, and that is deliberate.
//
// These used to be forward declarations, with the QML-facing Q_PROPERTYs declared `QObject*` so
// the includes could be avoided. That saved rebuilds and cost type information at the QML
// boundary — which is a bad trade, because the type information is what four separate tools use
// to find bugs before a user does:
//
//   - qmllint cannot check a property behind a QObject*. That blinded it to 1,310 QML call
//     sites across 281 distinct settings, so `Settings.brew.slectedFlushPreset` compiled, linted
//     clean, and failed silently at runtime — the exact class of defect that shipped in 2.0.1
//     as #1661.
//   - qmlcachegen cannot resolve those bindings ahead of time and falls back to a runtime lookup.
//   - The QML language server cannot autocomplete or navigate them.
//   - A reader of this header cannot tell what is behind `Settings.brew`.
//
// A pointer Q_PROPERTY needs a COMPLETE type: moc must build a metatype for it, and a forward
// declaration fails the build outright ("Pointer Meta Types must either point to fully-defined
// types or be declared with Q_DECLARE_OPAQUE_POINTER"). The Q_DECLARE_OPAQUE_POINTER escape
// hatch is NOT a way around this — it compiles and satisfies qmllint, then hands QML a
// `QVariant(SettingsBrew*)` instead of an object, so every property and method under
// `Settings.<domain>` fails at runtime. That was tried and reverted; see
// tst_settings::qmlChainsThroughDomainSubObjects, which pins the working behaviour.
//
// The cost, measured rather than assumed: 86 translation units include this header, and 49 of
// them already included at least one domain header, so the marginal blast is 37 TUs on a domain
// header edit (~2,450 lines of plain QObject declarations, no templates). The domain SPLIT still
// does its job — implementations stay in their own .cpp files — and the recompile win that
// motivated the split is unaffected by declaring the façade's property types honestly.
//
// C++ callers may still use the typed accessors (e.g. `settings->mqtt()`); they no longer need
// to include the domain header themselves.
#include "settings_mqtt.h"
#include "settings_autowake.h"
#include "settings_hardware.h"
#include "settings_ai.h"
#include "settings_theme.h"
#include "settings_visualizer.h"
#include "settings_mcp.h"
#include "settings_brew.h"
#include "settings_dye.h"
#include "settings_network.h"
#include "settings_app.h"
#include "settings_calibration.h"


class Settings : public QObject {
    Q_OBJECT

    // Domain sub-objects, declared with their concrete types so every tool that reads this
    // header can follow `Settings.mqtt.mqttEnabled` through to the property. See the file
    // header for why this is worth the includes, and what breaks if you try to avoid them.
    //
    // Required prerequisite: each sub-object type must also be a QML-known type. That
    // registration happens at COMPILE time via QML_FOREIGN in settings_qml.h (it used to be
    // qmlRegisterUncreatableType<> calls in main.cpp, which qmltyperegistrar cannot see).
    // Without it QML can't discover the type and chained access resolves to `undefined` at
    // runtime while still compiling clean.
    Q_PROPERTY(SettingsMqtt* mqtt READ mqtt CONSTANT)
    Q_PROPERTY(SettingsAutoWake* autoWake READ autoWake CONSTANT)
    Q_PROPERTY(SettingsHardware* hardware READ hardware CONSTANT)
    Q_PROPERTY(SettingsAI* ai READ ai CONSTANT)
    Q_PROPERTY(SettingsTheme* theme READ theme CONSTANT)
    Q_PROPERTY(SettingsVisualizer* visualizer READ visualizer CONSTANT)
    Q_PROPERTY(SettingsMcp* mcp READ mcp CONSTANT)
    Q_PROPERTY(SettingsBrew* brew READ brew CONSTANT)
    Q_PROPERTY(SettingsDye* dye READ dye CONSTANT)
    Q_PROPERTY(SettingsNetwork* network READ network CONSTANT)
    Q_PROPERTY(SettingsApp* app READ app CONSTANT)
    Q_PROPERTY(SettingsCalibration* calibration READ calibration CONSTANT)

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

#ifdef DECENZA_TESTING
    // The isolated store location test builds use in place of the real one, so
    // a test run cannot clobber a developer's actual settings on the same
    // machine (e.g. tst_aimanager.cpp's `setOpenaiApiKey("sk-test")` landing on
    // a real API key). PID-scoped, so concurrent and repeated runs don't
    // collide.
    //
    // Call sites don't reference this directly — AppSettings resolves here under
    // DECENZA_TESTING (see appsettings.cpp), which is what makes the isolation
    // impossible to bypass. Tests seeding raw pre-construction state should
    // construct an `AppSettings` like production code does.
    //
    // Defined inline (not in settings.cpp) so lean test binaries that link only
    // a single settings_<domain>.cpp — not the whole settings.cpp — still
    // resolve the symbol.
    static QString testQSettingsPath()
    {
        return QDir::tempPath() + QStringLiteral("/decenza_test_settings_%1.ini")
            .arg(QCoreApplication::applicationPid());
    }
#endif

    // Domain sub-object accessors (typed, for C++ callers). The domain headers are included
    // above, so a caller can dereference these without including anything further.
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

    // Does this key set belong to an installation that has never been used?
    //
    // Not simply "no keys at all". One-time migrations run before Settings is
    // constructed (see runSettingsStoreMigrationOnce() in main.cpp) and stamp their
    // done-flags into the store unconditionally — including on a brand-new install,
    // where there is nothing to migrate but the flag is still recorded. Counting
    // those would make every install look like an upgrade, and the one-shot blocks
    // in the constructor would seed legacy defaults for users who should get the
    // current ones. Migration bookkeeping is not user state, so it does not count.
    //
    // Static and pure so the rule is testable without constructing a Settings.
    static bool looksLikeFreshInstall(const QStringList& existingKeys)
    {
        return std::none_of(existingKeys.cbegin(), existingKeys.cend(),
                            [](const QString& key) {
                                return !key.startsWith(QLatin1String("migration/"));
                            });
    }

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

    mutable AppSettings m_settings;

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
};
