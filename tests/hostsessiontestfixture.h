#pragma once

#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtTest>

#include "core/packageidentity.h"
#include "core/graphpreferences.h"
#include "network/sessionappearance.h"
#include "network/steamuistate.h"
#include "remote/hostsessionservice.h"
#include "rep_hostsessionservice_source.h"
#include "webclient/remotesessionbackend.h"

namespace HostSessionTest {
class Source final : public HostSessionStateSimpleSource
{
public:
    using HostSessionStateSimpleSource::setReady;
    void setSnapshot(QVariantMap values) override
    {
        if (values.contains(QStringLiteral("appearance")))
            setAppearanceSnapshot(values.take(QStringLiteral("appearance")).toMap());
        HostSessionStateSimpleSource::setSnapshot(values);
    }
};

inline QString hostId() { return QStringLiteral("7a54d42f-93de-48a7-ae78-38925bd0c3e1"); }

inline QVariantMap appearance(const QString& preset, bool fahrenheit, int flashPhase)
{
    QVariantMap customColors;
    for (const QString& key : SessionAppearance::defaultThemeColorKeys())
        customColors.insert(key, QStringLiteral("#123456"));
    customColors.insert(QStringLiteral("conductanceColor"), QStringLiteral("#00d2d3"));

    return {
        {QStringLiteral("darkMode"), true},
        {QStringLiteral("fahrenheit"), fahrenheit},
        {QStringLiteral("fontSizes"), QVariantMap{
            {QStringLiteral("headingSize"), 32}, {QStringLiteral("titleSize"), 24},
            {QStringLiteral("subtitleSize"), 18}, {QStringLiteral("bodySize"), 18},
            {QStringLiteral("labelSize"), 14}, {QStringLiteral("captionSize"), 12},
            {QStringLiteral("valueSize"), 48}, {QStringLiteral("timerSize"), 72}
        }},
        {QStringLiteral("background"), QVariantMap{
            {QStringLiteral("source"), preset.isEmpty() ? QStringLiteral("none")
                                                         : QStringLiteral("colour")},
            {QStringLiteral("patternId"), QString()},
            {QStringLiteral("patternInk"), QStringLiteral("#ffffff")},
            {QStringLiteral("imageRevision"), QString(32, QLatin1Char('0'))}
        }},
        {QStringLiteral("themeInputs"), QVariantMap{
            {QStringLiteral("customThemeColors"), customColors},
            {QStringLiteral("backgroundPreset"), preset},
            {QStringLiteral("glassChrome"), false},
            {QStringLiteral("flashColorName"), QStringLiteral("accentColor")},
            {QStringLiteral("flashPhase"), flashPhase},
            {QStringLiteral("bundledFontFamily"), QStringLiteral("Decenza Sans")},
            {QStringLiteral("symbolFontFamily"), QStringLiteral("Noto Sans Math")},
            {QStringLiteral("activeShader"), QString()},
            {QStringLiteral("shaderParams"), QVariantMap{}},
            {QStringLiteral("use12HourTime"), true},
            {QStringLiteral("backgroundShotAdvanced"), false}
        }},
        {QStringLiteral("graph"), GraphPreferences::defaults()},
    };
}


inline QVariantMap snapshot(const QVariantMap& appearanceValues, qint64 sequence,
                            const MachineUiSnapshot& machineUi = {},
                            const SteamUiState& accessibility = {})
{
    return {
        {QStringLiteral("packageId"), QString::fromLatin1(decenzaPackageId())},
        {QStringLiteral("hostId"), hostId()},
        {QStringLiteral("sequence"), sequence},
        {QStringLiteral("appearance"), appearanceValues},
        {QStringLiteral("machineUi"), machineUi.toJson().toVariantMap()},
        {QStringLiteral("pressureBar"), 8.2},
        {QStringLiteral("flowMlPerSecond"), 2.1},
        {QStringLiteral("accessibilityEnabled"), false},
        {QStringLiteral("extractionAnnouncementsEnabled"), accessibility.extractionAnnouncementsEnabled},
        {QStringLiteral("extractionAnnouncementInterval"), accessibility.extractionAnnouncementInterval},
        {QStringLiteral("extractionAnnouncementMode"), accessibility.extractionAnnouncementMode}
    };
}

class Fixture final : public QObject
{
public:
    Fixture()
    {
        source.setSnapshot(snapshot(appearance(QString(), false, 0), 0));
        source.setReady(true);
        if (!host.setHostUrl(QUrl(QStringLiteral("tcp://127.0.0.1:0")))
            || !host.enableRemoting<HostSessionStateSourceAPI>(&source)
            || !client.connectToNode(host.hostUrl()))
            qFatal("Could not start the shared host-session test fixture");
    }

    HostSessionService* attach(RemoteSessionBackend* backend, QQmlEngine* engine)
    {
        auto* service = HostSessionService::create(engine, engine);
        backend->setService(service);
        backend->setApiOrigin(QStringLiteral("http://127.0.0.1:8080"));
        backend->setBootstrapIdentity(QString::fromLatin1(decenzaPackageId()), hostId());
        connect(backend, &RemoteSessionBackend::connectionRequested, backend, [this, backend, service] {
            if (!backend->connectionWanted()) { service->setNode(nullptr); return; }
            backend->transportOpened();
            service->setNode(&client);
        });
        return service;
    }

    void setAppearance(const QVariantMap& value)
    {
        auto values = source.snapshot(); values.insert(QStringLiteral("appearance"), value);
        source.setSnapshot(values);
    }
    void setMachineUi(const MachineUiSnapshot& value) { machineUi = value; }
    void setSteamUi(const SteamUiState& value) { accessibility = value; }
    void send(const QVariantMap& value, qint64 sequence)
    { source.setSnapshot(snapshot(value, sequence, machineUi, accessibility)); }

    Source source;
    QRemoteObjectHost host;
    QRemoteObjectNode client;
    MachineUiSnapshot machineUi;
    SteamUiState accessibility;
};
}
