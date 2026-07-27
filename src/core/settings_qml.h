#pragma once

// QML registration for Settings, kept OUT of settings.h on purpose.
//
// settings.h is included by roughly a hundred translation units and by CLI tools that do not
// link Qt::Qml at all — `saw_parity` compiles settings.cpp against Core/Sql/Network/Gui/Bluetooth
// only, so a <QtQml/...> include in that header is a build break, not a style question. The
// header also documents that it forward-declares its twelve domain sub-objects specifically to
// keep the recompile blast down; pulling QtQml into it would work against the same goal.
//
// QML_FOREIGN exists for exactly this: annotate the type from somewhere else. Only the Decenza
// target compiles this file, so the QtQml dependency stops here.
//
// Why a singleton at all: `Settings` was a context property, and a context property is invisible
// to qmllint — it cannot resolve the name, so every one of the ~1,300 `Settings.<domain>.<prop>`
// references in QML counted as an unqualified-access warning. A compile-time registration puts
// the type in the module's generated Decenza.qmltypes, which is the only place qmllint learns
// about C++ types.
//
// NOTE: registering the type is necessary but NOT sufficient — main.cpp must also call
// qml_register_types_Decenza() explicitly, or none of this module's declarative types reach the
// runtime registry. See the comment at that call site; it is a Qt guard that this app trips.

#include <QtQml/qqmlregistration.h>
#include <QtQml/QQmlEngine>
#include <QtQml/QJSEngine>

// Brings Settings and, through it, all twelve domain classes — QML_FOREIGN needs each of them
// complete, and settings.h now declares its properties with those concrete types anyway.
#include "settings.h"

struct SettingsForeign
{
    Q_GADGET
    QML_FOREIGN(Settings)
    QML_SINGLETON
    QML_NAMED_ELEMENT(Settings)

public:
    // Set by main() before the engine loads. The engine does not own or create this object:
    // main holds it on the stack and hands `&settings` to most of the app long before any QML
    // exists, so the instance has to be published rather than constructed on demand.
    inline static Settings* s_singletonInstance = nullptr;

    static Settings* create(QQmlEngine*, QJSEngine* engine)
    {
        // The instance must already exist; this cannot construct or replace it.
        Q_ASSERT(s_singletonInstance);
        Q_ASSERT(engine->thread() == s_singletonInstance->thread());

        // Qt's own example additionally asserts that only ONE engine ever reaches the singleton.
        // Deliberately omitted: this app runs a second QQmlEngine for the GHC simulator window in
        // debug builds, and that engine is given `Settings` as a context property, which resolves
        // ahead of a type name. Either way it would be the same object — main's — so sharing it
        // is what already happens today and the assert would only fire on a correct program.
        QJSEngine::setObjectOwnership(s_singletonInstance, QJSEngine::CppOwnership);
        return s_singletonInstance;
    }
};

// The twelve domain sub-objects. Each is reachable only as `Settings.<domain>`, never
// constructed from QML — the same contract the runtime qmlRegisterUncreatableType<> calls in
// main.cpp used to express, moved to compile time and keeping the identical QML type names.
//
// Compile-time registration is what makes the difference for tooling: a runtime call is
// invisible to qmltyperegistrar, so qmllint could not resolve these types and reported every
// `Settings.<domain>.<prop>` as a missing property (measured: 1,079 of them). Registered here,
// the whole Settings tree is checkable, so a typo like `Settings.brew.slectedFlushPreset` is
// caught by the linter instead of silently evaluating to undefined at runtime.
// Written out one by one rather than generated from a macro: moc does not expand macros that
// declare a class containing Q_GADGET/Q_OBJECT, so a macro here compiles but registers nothing
// — the exact silent-success failure this change exists to eliminate.

struct SettingsMqttForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsMqtt)
    QML_NAMED_ELEMENT(SettingsMqttType)
    QML_UNCREATABLE("SettingsMqtt is created in C++")
};

struct SettingsAutoWakeForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsAutoWake)
    QML_NAMED_ELEMENT(SettingsAutoWakeType)
    QML_UNCREATABLE("SettingsAutoWake is created in C++")
};

struct SettingsHardwareForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsHardware)
    QML_NAMED_ELEMENT(SettingsHardwareType)
    QML_UNCREATABLE("SettingsHardware is created in C++")
};

struct SettingsAIForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsAI)
    QML_NAMED_ELEMENT(SettingsAIType)
    QML_UNCREATABLE("SettingsAI is created in C++")
};

struct SettingsThemeForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsTheme)
    QML_NAMED_ELEMENT(SettingsThemeType)
    QML_UNCREATABLE("SettingsTheme is created in C++")
};

struct SettingsVisualizerForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsVisualizer)
    QML_NAMED_ELEMENT(SettingsVisualizerType)
    QML_UNCREATABLE("SettingsVisualizer is created in C++")
};

struct SettingsMcpForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsMcp)
    QML_NAMED_ELEMENT(SettingsMcpType)
    QML_UNCREATABLE("SettingsMcp is created in C++")
};

struct SettingsBrewForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsBrew)
    QML_NAMED_ELEMENT(SettingsBrewType)
    QML_UNCREATABLE("SettingsBrew is created in C++")
};

struct SettingsDyeForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsDye)
    QML_NAMED_ELEMENT(SettingsDyeType)
    QML_UNCREATABLE("SettingsDye is created in C++")
};

struct SettingsNetworkForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsNetwork)
    QML_NAMED_ELEMENT(SettingsNetworkType)
    QML_UNCREATABLE("SettingsNetwork is created in C++")
};

struct SettingsAppForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsApp)
    QML_NAMED_ELEMENT(SettingsAppType)
    QML_UNCREATABLE("SettingsApp is created in C++")
};

struct SettingsCalibrationForeign
{
    Q_GADGET
    QML_FOREIGN(SettingsCalibration)
    QML_NAMED_ELEMENT(SettingsCalibrationType)
    QML_UNCREATABLE("SettingsCalibration is created in C++")
};
