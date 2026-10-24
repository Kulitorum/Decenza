#include "settingspreferencesservicesource.h"
#include "servicevalue.h"

#include "core/settings.h"
#include "core/settings_app.h"
#include "core/settings_brew.h"
#include "core/settings_hardware.h"
#include "core/settings_graph.h"
#include "core/settings_visualizer.h"
#include "core/settings_decent.h"
#include "core/settings_upload.h"
#include "core/settings_ai.h"
#include "core/settings_mcp.h"
#include "core/settings_network.h"
#include "core/batterymanager.h"
#include "screensaver/screensavervideomanager.h"
#include "network/shotreporter.h"
#include "core/accessibilitymanager.h"
#include "ai/aimanager.h"

#include <QMetaType>
#include <QUrl>


namespace {
const QStringList kDomains{
    QStringLiteral("app"), QStringLiteral("hardware"),
    QStringLiteral("brew"), QStringLiteral("visualizer"),
    QStringLiteral("battery"), QStringLiteral("screensaver"),
    QStringLiteral("shotMap"), QStringLiteral("connections"),
    QStringLiteral("upload"), QStringLiteral("accessibility"),
    QStringLiteral("ai"), QStringLiteral("mcp"), QStringLiteral("aiNetwork"),
    QStringLiteral("network"), QStringLiteral("graph")
};

bool exactBool(const QVariant& value, bool* result)
{
    if (value.metaType().id() != QMetaType::Bool)
        return false;
    *result = value.toBool();
    return true;
}

bool exactString(const QVariant& value, const QStringList& allowed, QString* result)
{
    if (value.metaType().id() != QMetaType::QString)
        return false;
    const QString text = value.toString();
    if (!allowed.contains(text))
        return false;
    *result = text;
    return true;
}

QString reasonForRequestStart(ServiceRequestIds::Start start)
{
    switch (start) {
    case ServiceRequestIds::Start::InvalidId: return QStringLiteral("invalidRequest");
    case ServiceRequestIds::Start::LimitReached: return QStringLiteral("busy");
    case ServiceRequestIds::Start::DuplicateId: return QStringLiteral("duplicateRequest");
    case ServiceRequestIds::Start::Accepted: break;
    }
    return {};
}

QVariantMap domainValues(const QVariantMap& values, const QString& domain)
{
    QVariantMap result;
    const QString prefix = domain + QLatin1Char('.');
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (it.key().startsWith(prefix))
            result.insert(it.key(), it.value());
    }
    return result;
}
} // namespace

NativeSettingsPreferencesSource::NativeSettingsPreferencesSource(Settings* settings,
                                                                 QObject* parent)
    : SettingsPreferencesStateSimpleSource(parent)
    , m_settings(settings)
{
    for (const QString& domain : kDomains)
        m_revisions.insert(domain, QVariant::fromValue<quint64>(0));

    if (m_settings) {
        const auto changed = [this](const QString& domain) {
            refreshDomain(domain);
        };
        auto* app = m_settings->app();
        connect(app, &SettingsApp::launcherModeChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::waterLevelDisplayUnitChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::temperatureUnitChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::waterRefillPointChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::refillKitOverrideChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::screenCaptureEnabledChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::developerTranslationUploadChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::hideGhcSimulatorChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::simulatedScaleEnabledChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::simulationModeChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::refractometerAutoTestChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::autoCheckUpdatesChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::betaUpdatesEnabledChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        connect(app, &SettingsApp::firmwareEarlyAccessChanged, this,
                [changed] { changed(QStringLiteral("app")); });
        const auto connectionsChanged = [changed] { changed(QStringLiteral("connections")); };
        connect(m_settings, &Settings::keepScaleOnChanged, this, connectionsChanged);
        connect(m_settings, &Settings::useFlowScaleChanged, this, connectionsChanged);
        connect(m_settings, &Settings::showScaleDialogsChanged, this, connectionsChanged);
        connect(m_settings, &Settings::usbSerialEnabledChanged, this, connectionsChanged);

        auto* graph = m_settings->graph();
        connect(graph, &SettingsGraph::advancedModeChanged, this,
                [changed] { changed(QStringLiteral("graph")); });
        connect(graph, &SettingsGraph::flowMultiplierChanged, this,
                [changed] { changed(QStringLiteral("graph")); });

        auto* hardware = m_settings->hardware();
        connect(hardware, &SettingsHardware::steamTwoTapStopChanged, this,
                [changed] { changed(QStringLiteral("hardware")); });

        auto* brew = m_settings->brew();
        connect(brew, &SettingsBrew::keepWarmWhenIdleChanged, this,
                [changed] { changed(QStringLiteral("brew")); });
        connect(brew, &SettingsBrew::letRecipeDecideChanged, this,
                [changed] { changed(QStringLiteral("brew")); });
        connect(brew, &SettingsBrew::steamAutoFlushSecondsChanged, this,
                [changed] { changed(QStringLiteral("brew")); });

        auto* visualizer = m_settings->visualizer();
        connect(visualizer, &SettingsVisualizer::visualizerShowAfterShotChanged, this,
                [changed] { changed(QStringLiteral("visualizer")); });
        connect(visualizer, &SettingsVisualizer::visualizerClearNotesOnStartChanged, this,
                [changed] { changed(QStringLiteral("visualizer")); });
        connect(visualizer, &SettingsVisualizer::communitySearchEnabledChanged, this,
                [changed] { changed(QStringLiteral("visualizer")); });
        connect(visualizer, &SettingsVisualizer::communityMinEnjoymentChanged, this,
                [changed] { changed(QStringLiteral("visualizer")); });
        connect(visualizer, &SettingsVisualizer::visualizerEnabledChanged, this,
                [changed] { changed(QStringLiteral("upload")); });
        connect(m_settings->decent(), &SettingsDecent::enabledChanged, this,
                [changed] { changed(QStringLiteral("upload")); });
        connect(m_settings->upload(), &SettingsUpload::autoUploadChanged, this,
                [changed] { changed(QStringLiteral("upload")); });
        connect(m_settings->upload(), &SettingsUpload::autoUpdateChanged, this,
                [changed] { changed(QStringLiteral("upload")); });
        connect(m_settings->upload(), &SettingsUpload::minDurationChanged, this,
                [changed] { changed(QStringLiteral("upload")); });

        auto* autoWake = m_settings->autoWake();
        connect(autoWake, &SettingsAutoWake::autoWakeEnabledChanged, this,
                [changed] { changed(QStringLiteral("screensaver")); });
        connect(autoWake, &SettingsAutoWake::autoWakeScheduleChanged, this,
                [changed] { changed(QStringLiteral("screensaver")); });
        connect(autoWake, &SettingsAutoWake::autoWakeStayAwakeEnabledChanged, this,
                [changed] { changed(QStringLiteral("screensaver")); });
        connect(autoWake, &SettingsAutoWake::autoWakeStayAwakeMinutesChanged, this,
                [changed] { changed(QStringLiteral("screensaver")); });
        connect(m_settings, &Settings::valueChanged, this, [changed](const QString& key) {
            if (key == QLatin1String("autoSleepMinutes"))
                changed(QStringLiteral("screensaver"));
            else if (key == QLatin1String("ui/configurePageScale"))
                changed(QStringLiteral("app"));
        });

        connect(m_settings->ai(), &SettingsAI::configurationChanged, this,
                [changed] { changed(QStringLiteral("ai")); });
        connect(m_settings->ai(), &SettingsAI::tasteIntakeOnAskChanged, this,
                [changed] { changed(QStringLiteral("ai")); });
        auto* mcp = m_settings->mcp();
        connect(mcp, &SettingsMcp::mcpEnabledChanged, this, [changed] { changed(QStringLiteral("mcp")); });
        connect(mcp, &SettingsMcp::mcpAccessLevelChanged, this, [changed] { changed(QStringLiteral("mcp")); });
        connect(mcp, &SettingsMcp::mcpConfirmationLevelChanged, this, [changed] { changed(QStringLiteral("mcp")); });
        connect(mcp, &SettingsMcp::remoteMcpEnabledChanged, this, [changed] { changed(QStringLiteral("mcp")); });
        connect(mcp, &SettingsMcp::remoteMcpModeChanged, this, [changed] { changed(QStringLiteral("mcp")); });
        connect(mcp, &SettingsMcp::remoteMcpPortChanged, this, [changed] { changed(QStringLiteral("mcp")); });
        connect(mcp, &SettingsMcp::remoteMcpCustomBaseUrlChanged, this, [changed] { changed(QStringLiteral("mcp")); });
        connect(m_settings->network(), &SettingsNetwork::discussShotAppChanged, this,
                [changed] { changed(QStringLiteral("aiNetwork")); });
        connect(m_settings->network(), &SettingsNetwork::discussShotCustomUrlChanged, this,
                [changed] { changed(QStringLiteral("aiNetwork")); });
        connect(m_settings->network(), &SettingsNetwork::claudeRcSessionUrlChanged, this,
                [changed] { changed(QStringLiteral("aiNetwork")); });
        connect(m_settings->network(), &SettingsNetwork::shotServerEnabledChanged, this,
                [changed] { changed(QStringLiteral("network")); });
        connect(m_settings->network(), &SettingsNetwork::webSecurityEnabledChanged, this,
                [changed] { changed(QStringLiteral("network")); });
        connect(m_settings->network(), &SettingsNetwork::exportShotsToFileChanged, this,
                [changed] { changed(QStringLiteral("network")); });

        connect(m_settings, &QObject::destroyed, this, [this] {
            m_settings = nullptr;
            setReady(false);
        });
        refresh();
    } else {
        setPreferences({});
        setReady(false);
    }
}

void NativeSettingsPreferencesSource::setScreensaverManager(ScreensaverVideoManager* manager)
{
    if (m_screensaverManager == manager)
        return;
    if (m_screensaverManager)
        disconnect(m_screensaverManager, nullptr, this, nullptr);
    m_screensaverManager = manager;
    if (!manager) {
        refreshDomain(QStringLiteral("screensaver"));
        return;
    }
    const auto changed = [this] { refreshDomain(QStringLiteral("screensaver")); };
    connect(manager, &ScreensaverVideoManager::enabledChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::cacheEnabledChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::streamingFallbackEnabledChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::selectedCategoryIdChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::imageDisplayDurationChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::showDateOnPersonalChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::screensaverTypeChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::pipesSpeedChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::pipesCameraSpeedChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::flipClockUse3DChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::videosShowClockChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::pipesShowClockChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::attractorShowClockChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::shotMapShapeChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::shotMapTextureChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::shotMapShowClockChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::shotMapShowProfilesChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::shotMapShowTerminatorChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::overlayShowWaterLevelChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::overlayShowShotPlanChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::overlayShowBatteryChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::overlayLinkButtonEnabledChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::overlayLinkButtonLabelChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::overlayLinkButtonUrlChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::dimPercentChanged, this, changed);
    connect(manager, &ScreensaverVideoManager::dimDelayMinutesChanged, this, changed);
    connect(manager, &QObject::destroyed, this, [this] { m_screensaverManager = nullptr; refreshDomain(QStringLiteral("screensaver")); });
    refreshDomain(QStringLiteral("screensaver"));
}

void NativeSettingsPreferencesSource::setBatteryManager(BatteryManager* manager)
{
    if (m_batteryManager == manager)
        return;
    if (m_batteryManager)
        disconnect(m_batteryManager, nullptr, this, nullptr);
    m_batteryManager = manager;
    if (!manager) {
        refreshDomain(QStringLiteral("battery"));
        return;
    }
    const auto changed = [this] { refreshDomain(QStringLiteral("battery")); };
    connect(manager, &BatteryManager::chargingModeChanged, this, changed);
    connect(manager, &BatteryManager::usbChargerEnabledChanged, this, changed);
    connect(manager, &QObject::destroyed, this, [this] {
        m_batteryManager = nullptr;
        refreshDomain(QStringLiteral("battery"));
    });
    refreshDomain(QStringLiteral("battery"));
}

void NativeSettingsPreferencesSource::setShotReporter(ShotReporter* reporter)
{
    if (m_shotReporter == reporter)
        return;
    if (m_shotReporter)
        disconnect(m_shotReporter, nullptr, this, nullptr);
    m_shotReporter = reporter;
    m_shotMapGpsEnabled = reporter && reporter->isGpsEnabled();
    if (!reporter) {
        refreshDomain(QStringLiteral("shotMap"));
        return;
    }
    const auto changed = [this, reporter] {
        m_shotMapGpsEnabled = reporter->isGpsEnabled();
        refreshDomain(QStringLiteral("shotMap"));
    };
    connect(reporter, &ShotReporter::enabledChanged, this, changed);
    connect(reporter, &ShotReporter::manualCityChanged, this, changed);
    connect(reporter, &ShotReporter::locationStatusChanged, this, changed);
    connect(reporter, &ShotReporter::lastErrorChanged, this, changed);
    connect(reporter, &QObject::destroyed, this, [this] {
        m_shotReporter = nullptr;
        m_shotMapGpsEnabled = false;
        refreshDomain(QStringLiteral("shotMap"));
    });
    refreshDomain(QStringLiteral("shotMap"));
}

void NativeSettingsPreferencesSource::setAccessibilityManager(AccessibilityManager* manager)
{
    if (m_accessibilityManager == manager)
        return;
    if (m_accessibilityManager)
        disconnect(m_accessibilityManager, nullptr, this, nullptr);
    m_accessibilityManager = manager;
    if (!manager) {
        refreshDomain(QStringLiteral("accessibility"));
        return;
    }
    const auto changed = [this] { refreshDomain(QStringLiteral("accessibility")); };
    connect(manager, &AccessibilityManager::enabledChanged, this, changed);
    connect(manager, &AccessibilityManager::ttsEnabledChanged, this, changed);
    connect(manager, &AccessibilityManager::tickEnabledChanged, this, changed);
    connect(manager, &AccessibilityManager::tickSoundIndexChanged, this, changed);
    connect(manager, &AccessibilityManager::tickVolumeChanged, this, changed);
    connect(manager, &AccessibilityManager::extractionAnnouncementsEnabledChanged, this, changed);
    connect(manager, &AccessibilityManager::extractionAnnouncementIntervalChanged, this, changed);
    connect(manager, &AccessibilityManager::extractionAnnouncementModeChanged, this, changed);
    connect(manager, &QObject::destroyed, this, [this] {
        m_accessibilityManager = nullptr;
        refreshDomain(QStringLiteral("accessibility"));
    });
    refreshDomain(QStringLiteral("accessibility"));
}

void NativeSettingsPreferencesSource::setAIManager(AIManager* manager)
{
    if (m_aiManager == manager)
        return;
    if (m_aiManager)
        disconnect(m_aiManager, nullptr, this, nullptr);
    m_aiManager = manager;
    if (!manager) {
        refreshDomain(QStringLiteral("ai"));
        return;
    }
    const auto changed = [this] { refreshDomain(QStringLiteral("ai")); };
    connect(manager, &AIManager::configurationChanged, this, changed);
    connect(manager, &AIManager::providerChanged, this, changed);
    connect(manager, &AIManager::modelSummaryChanged, this, changed);
    connect(manager, &AIManager::ollamaModelsChanged, this, changed);
    connect(manager, &QObject::destroyed, this, [this] {
        m_aiManager = nullptr;
        refreshDomain(QStringLiteral("ai"));
    });
    refreshDomain(QStringLiteral("ai"));
}

void NativeSettingsPreferencesSource::refresh()
{
    if (!m_settings) {
        setReady(false);
        return;
    }

    const QVariantMap current = capturePreferences();
    for (const QString& domain : kDomains) {
        const QVariantMap oldDomain = domainValues(m_preferences, domain);
        const QVariantMap newDomain = domainValues(current, domain);
        if (oldDomain != newDomain) {
            m_revisions.insert(domain,
                QVariant::fromValue<quint64>(domainRevision(domain) + 1));
        }
    }
    m_preferences = current;
    setPreferences(m_preferences);
    setRevisions(m_revisions);
    setReady(true);
}

void NativeSettingsPreferencesSource::refreshShotMapLocation()
{
    if (m_shotReporter) {
        m_shotMapGpsEnabled = m_shotReporter->isGpsEnabled();
        refreshDomain(QStringLiteral("shotMap"));
        m_shotReporter->refreshLocation();
    }
}

void NativeSettingsPreferencesSource::openShotMapLocationSettings()
{
    if (m_shotReporter)
        m_shotReporter->openLocationSettings();
}

void NativeSettingsPreferencesSource::refreshDomain(const QString& domain)
{
    if (!m_settings || !kDomains.contains(domain))
        return;
    const QVariantMap current = capturePreferences();
    if (domainValues(current, domain) == domainValues(m_preferences, domain))
        return;

    m_revisions.insert(domain,
        QVariant::fromValue<quint64>(domainRevision(domain) + 1));
    m_preferences = current;
    setPreferences(m_preferences);
    setRevisions(m_revisions);
}

QVariantMap NativeSettingsPreferencesSource::capturePreferences() const
{
    if (!m_settings)
        return {};

    const auto* app = m_settings->app();
    const auto* hardware = m_settings->hardware();
    const auto* brew = m_settings->brew();
    const auto* visualizer = m_settings->visualizer();
    QVariantMap result{
        {QStringLiteral("app.launcherMode"), app->launcherMode()},
        {QStringLiteral("app.simulatorAvailable"), app->simulatorAvailable()},
        {QStringLiteral("app.isDebugBuild"), app->isDebugBuild()},
        {QStringLiteral("app.configurePageScale"), m_settings->value(QStringLiteral("ui/configurePageScale"), false)},
        {QStringLiteral("app.simulationMode"), app->simulationMode()},
        {QStringLiteral("app.refractometerAutoTest"), app->refractometerAutoTest()},
        {QStringLiteral("app.autoCheckUpdates"), app->autoCheckUpdates()},
        {QStringLiteral("app.betaUpdatesEnabled"), app->betaUpdatesEnabled()},
        {QStringLiteral("app.firmwareEarlyAccess"), app->firmwareEarlyAccess()},
        {QStringLiteral("connections.keepScaleOn"), m_settings->keepScaleOn()},
        {QStringLiteral("connections.useFlowScale"), m_settings->useFlowScale()},
        {QStringLiteral("connections.showScaleDialogs"), m_settings->showScaleDialogs()},
        {QStringLiteral("connections.usbSerialEnabled"), m_settings->usbSerialEnabled()},
        {QStringLiteral("app.waterLevelDisplayUnit"), app->waterLevelDisplayUnit()},
        {QStringLiteral("app.temperatureUnit"), app->temperatureUnit()},
        {QStringLiteral("app.waterRefillPoint"), app->waterRefillPoint()},
        {QStringLiteral("app.refillKitOverride"), app->refillKitOverride()},
        {QStringLiteral("app.screenCaptureEnabled"), app->screenCaptureEnabled()},
        {QStringLiteral("app.developerTranslationUpload"), app->developerTranslationUpload()},
        {QStringLiteral("app.hideGhcSimulator"), app->hideGhcSimulator()},
        {QStringLiteral("app.simulatedScaleEnabled"), app->simulatedScaleEnabled()},
        {QStringLiteral("graph.advancedMode"), m_settings->graph()->advancedMode()},
        {QStringLiteral("graph.flowMultiplier"), m_settings->graph()->flowMultiplier()},
        {QStringLiteral("hardware.steamTwoTapStop"), hardware->steamTwoTapStop()},
        {QStringLiteral("brew.keepWarmWhenIdle"), brew->keepWarmWhenIdle()},
        {QStringLiteral("brew.letRecipeDecide"), brew->letRecipeDecide()},
        {QStringLiteral("brew.steamAutoFlushSeconds"), brew->steamAutoFlushSeconds()},
        {QStringLiteral("visualizer.visualizerShowAfterShot"), visualizer->visualizerShowAfterShot()},
        {QStringLiteral("visualizer.visualizerClearNotesOnStart"), visualizer->visualizerClearNotesOnStart()},
        {QStringLiteral("visualizer.communitySearchEnabled"), visualizer->communitySearchEnabled()},
        {QStringLiteral("visualizer.communityMinEnjoyment"), visualizer->communityMinEnjoyment()},
        {QStringLiteral("upload.visualizerEnabled"), visualizer->visualizerEnabled()},
        {QStringLiteral("upload.decentEnabled"), m_settings->decent()->enabled()},
        {QStringLiteral("upload.autoUpload"), m_settings->upload()->autoUpload()},
        {QStringLiteral("upload.autoUpdate"), m_settings->upload()->autoUpdate()},
        {QStringLiteral("upload.minDuration"), m_settings->upload()->minDuration()},
    };
    const auto* ai = m_settings->ai();
    result.insert(QStringLiteral("ai.aiProvider"), ai->aiProvider());
    result.insert(QStringLiteral("ai.hasOpenaiApiKey"), !ai->openaiApiKey().isEmpty());
    result.insert(QStringLiteral("ai.hasAnthropicApiKey"), !ai->anthropicApiKey().isEmpty());
    result.insert(QStringLiteral("ai.hasGeminiApiKey"), !ai->geminiApiKey().isEmpty());
    result.insert(QStringLiteral("ai.hasOpenrouterApiKey"), !ai->openrouterApiKey().isEmpty());
    result.insert(QStringLiteral("ai.ollamaEndpoint"), ai->ollamaEndpoint());
    result.insert(QStringLiteral("ai.ollamaModel"), ai->ollamaModel());
    result.insert(QStringLiteral("ai.openaiEndpoint"), ai->openaiEndpoint());
    result.insert(QStringLiteral("ai.anthropicEndpoint"), ai->anthropicEndpoint());
    result.insert(QStringLiteral("ai.providerModels"), ai->providerModels().toVariantMap());
    result.insert(QStringLiteral("ai.tasteIntakeOnAsk"), ai->tasteIntakeOnAsk());

    const auto* mcp = m_settings->mcp();
    result.insert(QStringLiteral("mcp.mcpEnabled"), mcp->mcpEnabled());
    result.insert(QStringLiteral("mcp.hasApiKey"), !mcp->mcpApiKey().isEmpty());
    result.insert(QStringLiteral("mcp.mcpAccessLevel"), mcp->mcpAccessLevel());
    result.insert(QStringLiteral("mcp.mcpConfirmationLevel"), mcp->mcpConfirmationLevel());
    result.insert(QStringLiteral("mcp.remoteMcpEnabled"), mcp->remoteMcpEnabled());
    result.insert(QStringLiteral("mcp.remoteMcpMode"), mcp->remoteMcpMode());
    result.insert(QStringLiteral("mcp.remoteMcpPort"), mcp->remoteMcpPort());
    result.insert(QStringLiteral("mcp.remoteMcpCustomBaseUrl"), mcp->remoteMcpCustomBaseUrl());
    const auto* network = m_settings->network();
    result.insert(QStringLiteral("aiNetwork.discussShotApp"), network->discussShotApp());
    result.insert(QStringLiteral("aiNetwork.discussShotCustomUrl"), network->discussShotCustomUrl());
    result.insert(QStringLiteral("aiNetwork.hasClaudeRcSessionUrl"), !network->claudeRcSessionUrl().isEmpty());
    result.insert(QStringLiteral("aiNetwork.discussAppNone"), network->discussAppNone());
    result.insert(QStringLiteral("aiNetwork.discussAppClaudeDesktop"), network->discussAppClaudeDesktop());
    result.insert(QStringLiteral("network.shotServerEnabled"), network->shotServerEnabled());
    result.insert(QStringLiteral("network.webSecurityEnabled"), network->webSecurityEnabled());
    result.insert(QStringLiteral("network.exportShotsToFile"), network->exportShotsToFile());
    if (m_batteryManager) {
        result.insert(QStringLiteral("battery.chargingMode"), m_batteryManager->chargingMode());
        result.insert(QStringLiteral("battery.usbChargerEnabled"), m_batteryManager->usbChargerEnabled());
    }
    const auto* autoWake = m_settings->autoWake();
    result.insert(QStringLiteral("screensaver.autoSleepMinutes"),
                  m_settings->value(QStringLiteral("autoSleepMinutes"), 60));
    result.insert(QStringLiteral("screensaver.autoWakeEnabled"), autoWake->autoWakeEnabled());
    result.insert(QStringLiteral("screensaver.autoWakeSchedule"), autoWake->autoWakeSchedule());
    result.insert(QStringLiteral("screensaver.autoWakeStayAwakeEnabled"), autoWake->autoWakeStayAwakeEnabled());
    result.insert(QStringLiteral("screensaver.autoWakeStayAwakeMinutes"), autoWake->autoWakeStayAwakeMinutes());
    if (m_screensaverManager) {
        const auto* manager = m_screensaverManager.data();
        result.insert(QStringLiteral("screensaver.enabled"), manager->enabled());
        result.insert(QStringLiteral("screensaver.cacheEnabled"), manager->cacheEnabled());
        result.insert(QStringLiteral("screensaver.streamingFallbackEnabled"), manager->streamingFallbackEnabled());
        result.insert(QStringLiteral("screensaver.selectedCategoryId"), manager->selectedCategoryId());
        result.insert(QStringLiteral("screensaver.imageDisplayDuration"), manager->imageDisplayDuration());
        result.insert(QStringLiteral("screensaver.showDateOnPersonal"), manager->showDateOnPersonal());
        result.insert(QStringLiteral("screensaver.screensaverType"), manager->screensaverType());
        result.insert(QStringLiteral("screensaver.pipesSpeed"), manager->pipesSpeed());
        result.insert(QStringLiteral("screensaver.pipesCameraSpeed"), manager->pipesCameraSpeed());
        result.insert(QStringLiteral("screensaver.flipClockUse3D"), manager->flipClockUse3D());
        result.insert(QStringLiteral("screensaver.videosShowClock"), manager->videosShowClock());
        result.insert(QStringLiteral("screensaver.pipesShowClock"), manager->pipesShowClock());
        result.insert(QStringLiteral("screensaver.attractorShowClock"), manager->attractorShowClock());
        result.insert(QStringLiteral("screensaver.shotMapShape"), manager->shotMapShape());
        result.insert(QStringLiteral("screensaver.shotMapTexture"), manager->shotMapTexture());
        result.insert(QStringLiteral("screensaver.shotMapShowClock"), manager->shotMapShowClock());
        result.insert(QStringLiteral("screensaver.shotMapShowProfiles"), manager->shotMapShowProfiles());
        result.insert(QStringLiteral("screensaver.shotMapShowTerminator"), manager->shotMapShowTerminator());
        result.insert(QStringLiteral("screensaver.overlayShowWaterLevel"), manager->overlayShowWaterLevel());
        result.insert(QStringLiteral("screensaver.overlayShowShotPlan"), manager->overlayShowShotPlan());
        result.insert(QStringLiteral("screensaver.overlayShowBattery"), manager->overlayShowBattery());
        result.insert(QStringLiteral("screensaver.overlayLinkButtonEnabled"), manager->overlayLinkButtonEnabled());
        result.insert(QStringLiteral("screensaver.overlayLinkButtonLabel"), manager->overlayLinkButtonLabel());
        result.insert(QStringLiteral("screensaver.overlayLinkButtonUrl"), manager->overlayLinkButtonUrl());
        result.insert(QStringLiteral("screensaver.dimPercent"), manager->dimPercent());
        result.insert(QStringLiteral("screensaver.dimDelayMinutes"), manager->dimDelayMinutes());
    }
    if (m_shotReporter) {
        result.insert(QStringLiteral("shotMap.enabled"), m_shotReporter->isEnabled());
        result.insert(QStringLiteral("shotMap.manualCity"), m_shotReporter->manualCity());
        result.insert(QStringLiteral("shotMap.hasLocation"), m_shotReporter->hasLocation());
        result.insert(QStringLiteral("shotMap.usingManualCity"), m_shotReporter->usingManualCity());
        result.insert(QStringLiteral("shotMap.city"), m_shotReporter->currentCity());
        result.insert(QStringLiteral("shotMap.countryCode"), m_shotReporter->currentCountryCode());
        // ShotReporter already rounds these to 0.1 degrees for the native UI;
        // share only that existing privacy-preserving display precision.
        result.insert(QStringLiteral("shotMap.latitude"), m_shotReporter->latitude());
        result.insert(QStringLiteral("shotMap.longitude"), m_shotReporter->longitude());
        result.insert(QStringLiteral("shotMap.gpsEnabled"), m_shotMapGpsEnabled);
        result.insert(QStringLiteral("shotMap.lastError"), m_shotReporter->lastError());
    }
    if (m_accessibilityManager) {
        const auto* accessibility = m_accessibilityManager.data();
        result.insert(QStringLiteral("accessibility.enabled"), accessibility->enabled());
        result.insert(QStringLiteral("accessibility.ttsEnabled"), accessibility->ttsEnabled());
        result.insert(QStringLiteral("accessibility.tickEnabled"), accessibility->tickEnabled());
        result.insert(QStringLiteral("accessibility.tickSoundIndex"), accessibility->tickSoundIndex());
        result.insert(QStringLiteral("accessibility.tickVolume"), accessibility->tickVolume());
        result.insert(QStringLiteral("accessibility.extractionAnnouncementsEnabled"), accessibility->extractionAnnouncementsEnabled());
        result.insert(QStringLiteral("accessibility.extractionAnnouncementMode"), accessibility->extractionAnnouncementMode());
        result.insert(QStringLiteral("accessibility.extractionAnnouncementInterval"), accessibility->extractionAnnouncementInterval());
    }
    return result;
}

quint64 NativeSettingsPreferencesSource::domainRevision(const QString& domain) const
{
    return m_revisions.value(domain).toULongLong();
}

bool NativeSettingsPreferencesSource::validateAndApply(const QString& domain,
                                                       const QString& name,
                                                       const QVariant& value,
                                                       QString* reason)
{
    if (!m_settings) {
        *reason = QStringLiteral("unavailable");
        return false;
    }

    bool boolValue = false;
    int intValue = 0;
    QString stringValue;
    if (domain == QLatin1String("graph")) {
        if (name == QLatin1String("advancedMode")) {
            if (!exactBool(value, &boolValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            m_settings->graph()->setAdvancedMode(boolValue);
            return true;
        }
        if (name == QLatin1String("flowMultiplier")) {
            if (!ServiceValue::boundedInteger(value, SettingsGraph::kFlowMultiplierMin,
                                               SettingsGraph::kFlowMultiplierMax, &intValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            m_settings->graph()->setFlowMultiplier(intValue);
            return true;
        }
        *reason = QStringLiteral("invalidPreference");
        return false;
    }
    if (domain == QLatin1String("app")) {
        auto* app = m_settings->app();
        if (name == QLatin1String("launcherMode")
            || name == QLatin1String("screenCaptureEnabled")
            || name == QLatin1String("developerTranslationUpload")
            || name == QLatin1String("hideGhcSimulator")
            || name == QLatin1String("simulatedScaleEnabled")
            || name == QLatin1String("refractometerAutoTest")
            || name == QLatin1String("autoCheckUpdates")
            || name == QLatin1String("betaUpdatesEnabled")
            || name == QLatin1String("firmwareEarlyAccess")
            || name == QLatin1String("configurePageScale")) {
            if (!exactBool(value, &boolValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            if (name == QLatin1String("launcherMode")) app->setLauncherMode(boolValue);
            else if (name == QLatin1String("screenCaptureEnabled")) app->setScreenCaptureEnabled(boolValue);
            else if (name == QLatin1String("developerTranslationUpload")) app->setDeveloperTranslationUpload(boolValue);
            else if (name == QLatin1String("hideGhcSimulator")) app->setHideGhcSimulator(boolValue);
            else if (name == QLatin1String("simulatedScaleEnabled")) app->setSimulatedScaleEnabled(boolValue);
            else if (name == QLatin1String("refractometerAutoTest")) app->setRefractometerAutoTest(boolValue);
            else if (name == QLatin1String("autoCheckUpdates")) app->setAutoCheckUpdates(boolValue);
            else if (name == QLatin1String("betaUpdatesEnabled")) app->setBetaUpdatesEnabled(boolValue);
            else if (name == QLatin1String("firmwareEarlyAccess")) app->setFirmwareEarlyAccess(boolValue);
            else m_settings->setValue(QStringLiteral("ui/configurePageScale"), boolValue);
            return true;
        }
        if (name == QLatin1String("simulationMode")) {
            if (!exactBool(value, &boolValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            if (boolValue && !app->simulatorAvailable()) {
                *reason = QStringLiteral("unavailable");
                return false;
            }
            app->setSimulationMode(boolValue);
            return true;
        }
        if (name == QLatin1String("waterLevelDisplayUnit")) {
            if (!exactString(value, {QStringLiteral("percent"), QStringLiteral("ml")}, &stringValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            app->setWaterLevelDisplayUnit(stringValue);
            return true;
        }
        if (name == QLatin1String("temperatureUnit")) {
            if (!exactString(value, {QStringLiteral("celsius"), QStringLiteral("fahrenheit")}, &stringValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            app->setTemperatureUnit(stringValue);
            return true;
        }
        if (name == QLatin1String("waterRefillPoint")) {
            if (!ServiceValue::boundedInteger(value, 3, 70, &intValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            app->setWaterRefillPoint(intValue);
            return true;
        }
        if (name == QLatin1String("refillKitOverride")) {
            if (!ServiceValue::boundedInteger(value, 0, 2, &intValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            app->setRefillKitOverride(intValue);
            return true;
        }
    } else if (domain == QLatin1String("accessibility")) {
        auto* accessibility = m_accessibilityManager.data();
        if (!accessibility) {
            *reason = QStringLiteral("unavailable");
            return false;
        }
        if (name == QLatin1String("enabled") || name == QLatin1String("ttsEnabled")
            || name == QLatin1String("tickEnabled")
            || name == QLatin1String("extractionAnnouncementsEnabled")) {
            if (!exactBool(value, &boolValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            if (name == QLatin1String("enabled")) accessibility->setEnabled(boolValue);
            else if (name == QLatin1String("ttsEnabled")) accessibility->setTtsEnabled(boolValue);
            else if (name == QLatin1String("tickEnabled")) accessibility->setTickEnabled(boolValue);
            else accessibility->setExtractionAnnouncementsEnabled(boolValue);
            return true;
        }
        if (name == QLatin1String("tickSoundIndex")) {
            if (!ServiceValue::boundedInteger(value, 1, 4, &intValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            accessibility->setTickSoundIndex(intValue);
            return true;
        }
        if (name == QLatin1String("tickVolume")) {
            if (!ServiceValue::boundedInteger(value, 0, 100, &intValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            accessibility->setTickVolume(intValue);
            return true;
        }
        if (name == QLatin1String("extractionAnnouncementInterval")) {
            if (!ServiceValue::boundedInteger(value, 5, 30, &intValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            accessibility->setExtractionAnnouncementInterval(intValue);
            return true;
        }
        if (name == QLatin1String("extractionAnnouncementMode")) {
            if (!exactString(value, {QStringLiteral("timed"), QStringLiteral("milestones_only"), QStringLiteral("both")}, &stringValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            accessibility->setExtractionAnnouncementMode(stringValue);
            return true;
        }
        *reason = QStringLiteral("invalidPreference");
        return false;
    } else if (domain == QLatin1String("hardware")) {
        if (name == QLatin1String("steamTwoTapStop")) {
            if (!exactBool(value, &boolValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            m_settings->hardware()->setSteamTwoTapStop(boolValue);
            return true;
        }
    } else if (domain == QLatin1String("brew")) {
        auto* brew = m_settings->brew();
        if (name == QLatin1String("keepWarmWhenIdle")
            || name == QLatin1String("letRecipeDecide")) {
            if (!exactBool(value, &boolValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            const bool changed = (name == QLatin1String("keepWarmWhenIdle"))
                ? (brew->keepWarmWhenIdle() != boolValue)
                : (brew->letRecipeDecide() != boolValue);
            if (name == QLatin1String("keepWarmWhenIdle")) brew->setKeepWarmWhenIdle(boolValue);
            else brew->setLetRecipeDecide(boolValue);
            if (changed)
                emit steamSettingsApplyRequested();
            return true;
        }
        if (name == QLatin1String("steamAutoFlushSeconds")) {
            if (!ServiceValue::boundedInteger(value, 0, 60, &intValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            brew->setSteamAutoFlushSeconds(intValue);
            return true;
        }
    } else if (domain == QLatin1String("visualizer")) {
        if (name == QLatin1String("visualizerShowAfterShot")
            || name == QLatin1String("visualizerClearNotesOnStart")
            || name == QLatin1String("communitySearchEnabled")) {
            if (!exactBool(value, &boolValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            if (name == QLatin1String("visualizerShowAfterShot"))
                m_settings->visualizer()->setVisualizerShowAfterShot(boolValue);
            else if (name == QLatin1String("visualizerClearNotesOnStart"))
                m_settings->visualizer()->setVisualizerClearNotesOnStart(boolValue);
            else
                m_settings->visualizer()->setCommunitySearchEnabled(boolValue);
            return true;
        }
        if (name == QLatin1String("communityMinEnjoyment")) {
            if (!ServiceValue::boundedInteger(value, 0, 100, &intValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            m_settings->visualizer()->setCommunityMinEnjoyment(intValue);
            return true;
        }
    } else if (domain == QLatin1String("upload")) {
        if (name == QLatin1String("visualizerEnabled")
            || name == QLatin1String("decentEnabled")
            || name == QLatin1String("autoUpload")
            || name == QLatin1String("autoUpdate")) {
            if (!exactBool(value, &boolValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            if (name == QLatin1String("visualizerEnabled"))
                m_settings->visualizer()->setVisualizerEnabled(boolValue);
            else if (name == QLatin1String("decentEnabled"))
                m_settings->decent()->setEnabled(boolValue);
            else if (name == QLatin1String("autoUpload"))
                m_settings->upload()->setAutoUpload(boolValue);
            else
                m_settings->upload()->setAutoUpdate(boolValue);
            return true;
        }
        if (name == QLatin1String("minDuration")) {
            double seconds = 0.0;
            if (!ServiceValue::finiteNumber(value, &seconds) || seconds < 0.0 || seconds > 30.0) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            m_settings->upload()->setMinDuration(seconds);
            return true;
        }
        *reason = QStringLiteral("invalidPreference");
        return false;
    } else if (domain == QLatin1String("battery")) {
        auto* battery = m_batteryManager.data();
        if (!battery) {
            *reason = QStringLiteral("unavailable");
            return false;
        }
        if (name == QLatin1String("chargingMode")) {
            if (!ServiceValue::boundedInteger(value, BatteryManager::Off,
                                             BatteryManager::Night, &intValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            battery->setChargingMode(intValue);
            return true;
        }
        if (name == QLatin1String("usbChargerEnabled")) {
            if (!exactBool(value, &boolValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            battery->setUsbChargerEnabled(boolValue);
            return true;
        }
        *reason = QStringLiteral("invalidPreference");
        return false;
    } else if (domain == QLatin1String("screensaver")) {
        auto* manager = m_screensaverManager.data();
        auto* autoWake = m_settings->autoWake();
        if (name == QLatin1String("autoSleepMinutes")) {
            if (!ServiceValue::boundedInteger(value, 0, 240, &intValue) || intValue % 5 != 0) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            m_settings->setValue(QStringLiteral("autoSleepMinutes"), intValue);
            return true;
        }
        if (name == QLatin1String("autoWakeEnabled")) {
            if (!exactBool(value, &boolValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            autoWake->setAutoWakeEnabled(boolValue);
            return true;
        }
        if (name == QLatin1String("autoWakeSchedule")) {
            if (value.metaType().id() != QMetaType::QVariantList) { *reason = QStringLiteral("invalidValue"); return false; }
            const QVariantList schedule = value.toList();
            if (schedule.size() != 7) { *reason = QStringLiteral("invalidValue"); return false; }
            for (const QVariant& dayValue : schedule) {
                if (dayValue.metaType().id() != QMetaType::QVariantMap) { *reason = QStringLiteral("invalidValue"); return false; }
                const QVariantMap day = dayValue.toMap();
                bool enabled = false;
                int hour = 0;
                int minute = 0;
                if (day.size() != 3 || !day.contains(QStringLiteral("enabled"))
                    || !day.contains(QStringLiteral("hour")) || !day.contains(QStringLiteral("minute"))
                    || !exactBool(day.value(QStringLiteral("enabled")), &enabled)
                    || !ServiceValue::boundedInteger(day.value(QStringLiteral("hour")), 0, 23, &hour)
                    || !ServiceValue::boundedInteger(day.value(QStringLiteral("minute")), 0, 59, &minute)) {
                    *reason = QStringLiteral("invalidValue");
                    return false;
                }
            }
            autoWake->setAutoWakeSchedule(schedule);
            return true;
        }
        if (name == QLatin1String("autoWakeStayAwakeEnabled")) {
            if (!exactBool(value, &boolValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            autoWake->setAutoWakeStayAwakeEnabled(boolValue);
            return true;
        }
        if (name == QLatin1String("autoWakeStayAwakeMinutes")) {
            if (!ServiceValue::boundedInteger(value, 15, 720, &intValue) || intValue % 15 != 0) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            autoWake->setAutoWakeStayAwakeMinutes(intValue);
            return true;
        }
        if (!manager) {
            *reason = QStringLiteral("unavailable");
            return false;
        }
        if (name == QLatin1String("enabled") || name == QLatin1String("cacheEnabled")
            || name == QLatin1String("streamingFallbackEnabled") || name == QLatin1String("showDateOnPersonal")
            || name == QLatin1String("flipClockUse3D") || name == QLatin1String("videosShowClock")
            || name == QLatin1String("pipesShowClock") || name == QLatin1String("attractorShowClock")
            || name == QLatin1String("shotMapShowClock") || name == QLatin1String("shotMapShowProfiles")
            || name == QLatin1String("shotMapShowTerminator") || name == QLatin1String("overlayShowWaterLevel")
            || name == QLatin1String("overlayShowShotPlan") || name == QLatin1String("overlayShowBattery")
            || name == QLatin1String("overlayLinkButtonEnabled")) {
            if (!exactBool(value, &boolValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            if (name == QLatin1String("enabled")) manager->setEnabled(boolValue);
            else if (name == QLatin1String("cacheEnabled")) manager->setCacheEnabled(boolValue);
            else if (name == QLatin1String("streamingFallbackEnabled")) manager->setStreamingFallbackEnabled(boolValue);
            else if (name == QLatin1String("showDateOnPersonal")) manager->setShowDateOnPersonal(boolValue);
            else if (name == QLatin1String("flipClockUse3D")) manager->setFlipClockUse3D(boolValue);
            else if (name == QLatin1String("videosShowClock")) manager->setVideosShowClock(boolValue);
            else if (name == QLatin1String("pipesShowClock")) manager->setPipesShowClock(boolValue);
            else if (name == QLatin1String("attractorShowClock")) manager->setAttractorShowClock(boolValue);
            else if (name == QLatin1String("shotMapShowClock")) manager->setShotMapShowClock(boolValue);
            else if (name == QLatin1String("shotMapShowProfiles")) manager->setShotMapShowProfiles(boolValue);
            else if (name == QLatin1String("shotMapShowTerminator")) manager->setShotMapShowTerminator(boolValue);
            else if (name == QLatin1String("overlayShowWaterLevel")) manager->setOverlayShowWaterLevel(boolValue);
            else if (name == QLatin1String("overlayShowShotPlan")) manager->setOverlayShowShotPlan(boolValue);
            else if (name == QLatin1String("overlayShowBattery")) manager->setOverlayShowBattery(boolValue);
            else if (name == QLatin1String("overlayLinkButtonEnabled")) manager->setOverlayLinkButtonEnabled(boolValue);
            else { *reason = QStringLiteral("invalidPreference"); return false; }
            return true;
        }
        if (name == QLatin1String("screensaverType")) {
            if (!exactString(value, manager->availableScreensaverTypes(), &stringValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            manager->setScreensaverType(stringValue); return true;
        }
        if (name == QLatin1String("shotMapShape")) {
            if (!exactString(value, {QStringLiteral("flat"), QStringLiteral("globe")}, &stringValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            manager->setShotMapShape(stringValue); return true;
        }
        if (name == QLatin1String("shotMapTexture")) {
            if (!exactString(value, {QStringLiteral("dark"), QStringLiteral("bright"), QStringLiteral("satellite")}, &stringValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            manager->setShotMapTexture(stringValue); return true;
        }
        if (name == QLatin1String("overlayLinkButtonLabel") || name == QLatin1String("overlayLinkButtonUrl")) {
            if (value.metaType().id() != QMetaType::QString) { *reason = QStringLiteral("invalidValue"); return false; }
            const QString text = value.toString();
            if (text.size() > (name == QLatin1String("overlayLinkButtonLabel") ? 80 : 2048)) { *reason = QStringLiteral("invalidValue"); return false; }
            if (name == QLatin1String("overlayLinkButtonLabel")) manager->setOverlayLinkButtonLabel(text);
            else manager->setOverlayLinkButtonUrl(text);
            return true;
        }
        if (name == QLatin1String("selectedCategoryId")) {
            if (value.metaType().id() != QMetaType::QString) { *reason = QStringLiteral("invalidValue"); return false; }
            const QString categoryId = value.toString();
            bool known = categoryId == QLatin1String("personal");
            for (const QVariant& item : manager->categories())
                known = known || item.toMap().value(QStringLiteral("id")).toString() == categoryId;
            if (!known) { *reason = QStringLiteral("invalidValue"); return false; }
            manager->setSelectedCategoryId(categoryId); return true;
        }
        if (name == QLatin1String("imageDisplayDuration") || name == QLatin1String("dimPercent") || name == QLatin1String("dimDelayMinutes")) {
            const int minimum = name == QLatin1String("imageDisplayDuration") ? 1 : 0;
            const int maximum = name == QLatin1String("imageDisplayDuration") ? 300 : (name == QLatin1String("dimPercent") ? 100 : 45);
            if (!ServiceValue::boundedInteger(value, minimum, maximum, &intValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            if (name == QLatin1String("imageDisplayDuration")) manager->setImageDisplayDuration(intValue);
            else if (name == QLatin1String("dimPercent")) manager->setDimPercent(intValue);
            else manager->setDimDelayMinutes(intValue);
            return true;
        }
        if (name == QLatin1String("pipesSpeed") || name == QLatin1String("pipesCameraSpeed")) {
            double d = 0.0;
            const double low = name == QLatin1String("pipesSpeed") ? 0.1 : 10.0;
            const double high = name == QLatin1String("pipesSpeed") ? 2.0 : 300.0;
            if (!ServiceValue::finiteNumber(value, &d) || d < low || d > high) { *reason = QStringLiteral("invalidValue"); return false; }
            if (name == QLatin1String("pipesSpeed")) manager->setPipesSpeed(d);
            else manager->setPipesCameraSpeed(d);
            return true;
        }
        *reason = QStringLiteral("invalidPreference");
        return false;
    } else if (domain == QLatin1String("connections")) {
        if (name != QLatin1String("keepScaleOn") && name != QLatin1String("useFlowScale")
            && name != QLatin1String("showScaleDialogs") && name != QLatin1String("usbSerialEnabled")) {
            *reason = QStringLiteral("invalidPreference");
            return false;
        }
        if (!exactBool(value, &boolValue)) {
            *reason = QStringLiteral("invalidValue");
            return false;
        }
        if (name == QLatin1String("keepScaleOn")) m_settings->setKeepScaleOn(boolValue);
        else if (name == QLatin1String("useFlowScale")) m_settings->setUseFlowScale(boolValue);
        else if (name == QLatin1String("showScaleDialogs")) m_settings->setShowScaleDialogs(boolValue);
        else m_settings->setUsbSerialEnabled(boolValue);
        return true;
    } else if (domain == QLatin1String("shotMap")) {
        auto* reporter = m_shotReporter.data();
        if (!reporter) {
            *reason = QStringLiteral("unavailable");
            return false;
        }
        if (name == QLatin1String("enabled")) {
            if (!exactBool(value, &boolValue)) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            reporter->setEnabled(boolValue);
            return true;
        }
        if (name == QLatin1String("manualCity")) {
            if (value.metaType().id() != QMetaType::QString
                || value.toString().size() > 120) {
                *reason = QStringLiteral("invalidValue");
                return false;
            }
            reporter->setManualCity(value.toString());
            return true;
        }
        *reason = QStringLiteral("invalidPreference");
        return false;
    } else if (domain == QLatin1String("ai")) {
        auto* ai = m_settings->ai();
        if (name == QLatin1String("aiProvider")) {
            if (!exactString(value, {QStringLiteral("openai"), QStringLiteral("anthropic"),
                                     QStringLiteral("gemini"), QStringLiteral("openrouter"),
                                     QStringLiteral("ollama")}, &stringValue)) {
                *reason = QStringLiteral("invalidValue"); return false;
            }
            ai->setAiProvider(stringValue); return true;
        }
        const QStringList secretNames{QStringLiteral("openaiApiKey"), QStringLiteral("anthropicApiKey"),
                                      QStringLiteral("geminiApiKey"), QStringLiteral("openrouterApiKey")};
        if (secretNames.contains(name)) {
            if (value.metaType().id() != QMetaType::QString || value.toString().size() > 4096) {
                *reason = QStringLiteral("invalidValue"); return false;
            }
            if (name == QLatin1String("openaiApiKey")) ai->setOpenaiApiKey(value.toString());
            else if (name == QLatin1String("anthropicApiKey")) ai->setAnthropicApiKey(value.toString());
            else if (name == QLatin1String("geminiApiKey")) ai->setGeminiApiKey(value.toString());
            else ai->setOpenrouterApiKey(value.toString());
            return true;
        }
        if (name == QLatin1String("tasteIntakeOnAsk")) {
            if (!exactBool(value, &boolValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            ai->setTasteIntakeOnAsk(boolValue); return true;
        }
        if (name == QLatin1String("ollamaModel")) {
            if (value.metaType().id() != QMetaType::QString || value.toString().size() > 160) {
                *reason = QStringLiteral("invalidValue"); return false;
            }
            ai->setOllamaModel(value.toString()); return true;
        }
        if (name == QLatin1String("providerModel")) {
            if (value.metaType().id() != QMetaType::QVariantMap) { *reason = QStringLiteral("invalidValue"); return false; }
            const QVariantMap selection = value.toMap();
            const QString providerId = selection.value(QStringLiteral("providerId")).toString();
            const QString modelId = selection.value(QStringLiteral("modelId")).toString();
            if (selection.size() != 2
                || !QStringList{QStringLiteral("openai"), QStringLiteral("anthropic"),
                                QStringLiteral("gemini"), QStringLiteral("openrouter")}.contains(providerId)
                || modelId.isEmpty() || modelId.size() > 160 || !m_aiManager) {
                *reason = QStringLiteral("invalidValue"); return false;
            }
            bool knownModel = false;
            for (const QVariant& model : m_aiManager->availableModels(providerId))
                knownModel = knownModel || model.toMap().value(QStringLiteral("id")).toString() == modelId;
            if (!knownModel) { *reason = QStringLiteral("invalidValue"); return false; }
            ai->setProviderModel(providerId, modelId); return true;
        }
        if (name == QLatin1String("ollamaEndpoint") || name == QLatin1String("openaiEndpoint")
            || name == QLatin1String("anthropicEndpoint")) {
            if (value.metaType().id() != QMetaType::QString || value.toString().size() > 2048) {
                *reason = QStringLiteral("invalidValue"); return false;
            }
            if (value.toString().isEmpty()) {
                if (name == QLatin1String("ollamaEndpoint")) ai->setOllamaEndpoint(QString());
                else if (name == QLatin1String("openaiEndpoint")) ai->setOpenaiEndpoint(QString());
                else ai->setAnthropicEndpoint(QString());
                return true;
            }
            const QUrl url(value.toString());
            if (!url.isValid() || (url.scheme() != QLatin1String("http")
                                   && url.scheme() != QLatin1String("https"))) {
                *reason = QStringLiteral("invalidValue"); return false;
            }
            if (name == QLatin1String("ollamaEndpoint")) ai->setOllamaEndpoint(value.toString());
            else if (name == QLatin1String("openaiEndpoint")) ai->setOpenaiEndpoint(value.toString());
            else ai->setAnthropicEndpoint(value.toString());
            return true;
        }
        *reason = QStringLiteral("invalidPreference"); return false;
    } else if (domain == QLatin1String("mcp")) {
        auto* mcp = m_settings->mcp();
        if (name == QLatin1String("mcpEnabled") || name == QLatin1String("remoteMcpEnabled")) {
            if (!exactBool(value, &boolValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            if (name == QLatin1String("mcpEnabled")) mcp->setMcpEnabled(boolValue);
            else mcp->setRemoteMcpEnabled(boolValue);
            return true;
        }
        if (name == QLatin1String("mcpAccessLevel") || name == QLatin1String("mcpConfirmationLevel")) {
            if (!ServiceValue::boundedInteger(value, 0, 2, &intValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            if (name == QLatin1String("mcpAccessLevel")) mcp->setMcpAccessLevel(intValue);
            else mcp->setMcpConfirmationLevel(intValue);
            return true;
        }
        if (name == QLatin1String("remoteMcpMode")) {
            if (!exactString(value, {QStringLiteral("custom"), QStringLiteral("tailscale"), QStringLiteral("ngrok")}, &stringValue)) {
                *reason = QStringLiteral("invalidValue"); return false;
            }
            mcp->setRemoteMcpMode(stringValue); return true;
        }
        if (name == QLatin1String("remoteMcpPort")) {
            if (!ServiceValue::boundedInteger(value, 1024, 65535, &intValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            mcp->setRemoteMcpPort(intValue); return true;
        }
        if (name == QLatin1String("remoteMcpCustomBaseUrl")) {
            if (value.metaType().id() != QMetaType::QString || value.toString().size() > 2048) {
                *reason = QStringLiteral("invalidValue"); return false;
            }
            mcp->setRemoteMcpCustomBaseUrl(value.toString()); return true;
        }
        *reason = QStringLiteral("invalidPreference"); return false;
    } else if (domain == QLatin1String("network")) {
        auto* network = m_settings->network();
        if (name != QLatin1String("shotServerEnabled")
            && name != QLatin1String("webSecurityEnabled")
            && name != QLatin1String("exportShotsToFile")) {
            *reason = QStringLiteral("invalidPreference");
            return false;
        }
        if (!exactBool(value, &boolValue)) {
            *reason = QStringLiteral("invalidValue");
            return false;
        }
        if (name == QLatin1String("shotServerEnabled")) network->setShotServerEnabled(boolValue);
        else if (name == QLatin1String("webSecurityEnabled")) network->setWebSecurityEnabled(boolValue);
        else network->setExportShotsToFile(boolValue);
        return true;
    } else if (domain == QLatin1String("aiNetwork")) {
        auto* network = m_settings->network();
        if (name == QLatin1String("discussShotApp")) {
            if (!ServiceValue::boundedInteger(value, 0, 7, &intValue)) { *reason = QStringLiteral("invalidValue"); return false; }
            network->setDiscussShotApp(intValue); return true;
        }
        if (name == QLatin1String("discussShotCustomUrl") || name == QLatin1String("claudeRcSessionUrl")) {
            if (value.metaType().id() != QMetaType::QString || value.toString().size() > 4096) {
                *reason = QStringLiteral("invalidValue"); return false;
            }
            if (name == QLatin1String("discussShotCustomUrl")) network->setDiscussShotCustomUrl(value.toString());
            else network->setClaudeRcSessionUrl(value.toString());
            return true;
        }
        *reason = QStringLiteral("invalidPreference"); return false;
    }

    *reason = QStringLiteral("invalidPreference");
    return false;
}

void NativeSettingsPreferencesSource::setPreference(QString requestId, QString domain,
                                                      QString name, QVariant value,
                                                      quint64 expectedRevision)
{
    const auto start = m_requests.begin(requestId);
    if (start == ServiceRequestIds::Start::DuplicateId)
        return;
    if (start != ServiceRequestIds::Start::Accepted) {
        const QString reason = reasonForRequestStart(start);
        QMetaObject::invokeMethod(this, [this, requestId, domain, name, reason] {
            emit preferenceCompleted(requestId, domain, name, false, 0, reason);
        }, Qt::QueuedConnection);
        return;
    }

    if (!m_settings || !ready()) {
        finishPreference(requestId, domain, name, false, QStringLiteral("unavailable"));
        return;
    }
    if (!kDomains.contains(domain)) {
        finishPreference(requestId, domain, name, false, QStringLiteral("invalidDomain"));
        return;
    }
    if (expectedRevision != domainRevision(domain)) {
        finishPreference(requestId, domain, name, false, QStringLiteral("stalePreferences"));
        return;
    }

    QString reason;
    if (!validateAndApply(domain, name, value, &reason)) {
        finishPreference(requestId, domain, name, false,
                         reason.isEmpty() ? QStringLiteral("invalidPreference") : reason);
        return;
    }
    // Setter signals synchronously refresh the source snapshot and that domain's
    // revision. This fallback also covers a canonical setter that made no change.
    refreshDomain(domain);
    finishPreference(requestId, domain, name, true, {});
}

void NativeSettingsPreferencesSource::finishPreference(const QString& requestId,
                                                       const QString& domain,
                                                       const QString& name,
                                                       bool success,
                                                       const QString& reason)
{
    m_requests.finish(requestId);
    emit preferenceCompleted(requestId, domain, name, success,
                              domainRevision(domain), reason);
}
