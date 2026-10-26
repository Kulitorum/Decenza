#include "recipecontextservicesource.h"
#include "servicevalue.h"

#include "core/settings.h"
#include "core/settings_app.h"
#include "core/settings_brew.h"
#include "core/settings_dye.h"
#include "core/settings_hardware.h"
#include "core/settings_network.h"
#include "core/settings_graph.h"
#include "core/graphpreferences.h"
#include "core/machinecontrolpresets.h"
#include "history/shotprojection.h"
#include "history/shothistorystorage.h"

#include <QMetaType>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
int decimalCount(double value)
{
    QString text = QString::number(value, 'f', 3);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    const qsizetype dot = text.indexOf(QLatin1Char('.'));
    return dot < 0 ? 0 : int(text.size() - dot - 1);
}

int grindDecimals(double step, const QString& current)
{
    int decimals = decimalCount(step);
    static const QRegularExpression numeric(QStringLiteral("^-?\\d+(?:\\.\\d+)?$"));
    if (numeric.match(current.trimmed()).hasMatch()) {
        bool ok = false;
        const double value = current.trimmed().toDouble(&ok);
        if (ok)
            decimals = qMax(decimals, decimalCount(value));
    }
    return decimals;
}
} // namespace

namespace {
bool isRequestId(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return pattern.match(value).hasMatch();
}


bool boundedString(const QVariant& value, QString* result, int limit = 256)
{
    if (value.metaType().id() != QMetaType::QString)
        return false;
    const QString text = value.toString();
    if (text.size() > limit)
        return false;
    if (result)
        *result = text;
    return true;
}

using ServiceValue::finiteNumber;
using ServiceValue::boundedInteger;

bool grinderSetting(const QVariant& value, QString* result)
{
    return boundedString(value, result, 128);
}

bool grinderRpm(const QVariant& value, int* result)
{
    bool ok = false;
    const int rpm = value.toInt(&ok);
    if (!ok || rpm < 0 || rpm > 20000)
        return false;
    if (result)
        *result = rpm;
    return true;
}

bool presetName(const QVariant& value, QString* result, bool allowBlank = false)
{
    QString name;
    if (!boundedString(value, &name, 128) || (!allowBlank && name.trimmed().isEmpty()))
        return false;
    if (result)
        *result = name;
    return true;
}

bool expectedPreset(const QVariantMap& fields, const QVariantList& presets,
                    const QString& indexKey, const QString& nameKey, int* index)
{
    int parsedIndex = -1;
    QString expectedName;
    if (!boundedInteger(fields.value(indexKey), 0, std::numeric_limits<int>::max(), &parsedIndex)
        || !presetName(fields.value(nameKey), &expectedName, true))
        return false;
    if (parsedIndex >= presets.size()
        || presets.at(parsedIndex).toMap().value(QStringLiteral("name")).toString() != expectedName)
        return false;
    *index = parsedIndex;
    return true;
}

bool parseSortPreference(const QVariant& value, const QSet<QString>& allowedFields,
                         QString* field, QString* direction)
{
    if (value.metaType().id() != QMetaType::QVariantMap)
        return false;
    const QVariantMap sort = value.toMap();
    return ServiceValue::hasExactKeys(sort, {QStringLiteral("field"), QStringLiteral("direction")})
        && boundedString(sort.value(QStringLiteral("field")), field, 32)
        && boundedString(sort.value(QStringLiteral("direction")), direction, 4)
        && allowedFields.contains(*field)
        && (*direction == QLatin1String("ASC") || *direction == QLatin1String("DESC"));
}
} // namespace

NativeRecipeContextSource::NativeRecipeContextSource(Settings* settings,
                                                     ShotHistoryStorage* history,
                                                     QObject* parent)
    : RecipeContextStateSimpleSource(parent)
    , m_settings(settings)
    , m_history(history)
{
    if (m_settings) {
        const auto changed = [this] {
            if (!m_suppressPreferenceRefresh)
                refresh();
        };
        auto* app = m_settings->app();
        auto* brew = m_settings->brew();
        auto* dye = m_settings->dye();
        auto* network = m_settings->network();
        auto* graph = m_settings->graph();
        connect(m_settings, &Settings::valueChanged, this,
                [changed](const QString& key) {
            if (key == QLatin1String("shotReview/phaseSummaryExpanded"))
                changed();
            else if (key == QLatin1String("comparison/graphHeight"))
                changed();
            else if (key == QLatin1String("espresso/extractionView")
                     || key == QLatin1String("espresso/showPhaseIndicator")
                     || key == QLatin1String("espresso/showStats"))
                changed();
        });
        connect(app, &SettingsApp::autoLoadRevertMinutesChanged, this, changed);
        connect(brew, &SettingsBrew::lastUsedRatioChanged, this, changed);
        connect(brew, &SettingsBrew::ratioPreset1Changed, this, changed);
        connect(brew, &SettingsBrew::ratioPreset2Changed, this, changed);
        connect(brew, &SettingsBrew::ratioPreset3Changed, this, changed);
        connect(brew, &SettingsBrew::waterVesselPresetsChanged, this, changed);
        connect(brew, &SettingsBrew::selectedWaterVesselChanged, this, changed);
        connect(brew, &SettingsBrew::waterTemperatureChanged, this, changed);
        connect(brew, &SettingsBrew::waterVolumeChanged, this, changed);
        connect(brew, &SettingsBrew::waterVolumeModeChanged, this, changed);
        connect(brew, &SettingsBrew::flushPresetsChanged, this, changed);
        connect(brew, &SettingsBrew::selectedFlushPresetChanged, this, changed);
        connect(brew, &SettingsBrew::flushFlowChanged, this, changed);
        connect(brew, &SettingsBrew::flushSecondsChanged, this, changed);
        connect(brew, &SettingsBrew::doseCupTareWeightChanged, this, changed);
        connect(brew, &SettingsBrew::doseCaptureSoundEnabledChanged, this, changed);
        connect(dye, &SettingsDye::autoLoadRecipeIdChanged, this, changed);
        connect(dye, &SettingsDye::dyeBeanBrandChanged, this, changed);
        connect(dye, &SettingsDye::dyeBeanTypeChanged, this, changed);
        connect(dye, &SettingsDye::dyeBeanWeightChanged, this, changed);
        connect(dye, &SettingsDye::dyeRoastDateChanged, this, changed);
        connect(dye, &SettingsDye::dyeRoastLevelChanged, this, changed);
        connect(dye, &SettingsDye::dyeDrinkWeightChanged, this, changed);
        connect(dye, &SettingsDye::dyeBaristaChanged, this, changed);
        connect(dye, &SettingsDye::dyeBeanBaseIdChanged, this, changed);
        connect(dye, &SettingsDye::dyeBeanBaseDataChanged, this, changed);
        connect(dye, &SettingsDye::activeEquipmentIdChanged, this, changed);
        connect(dye, &SettingsDye::dyeEquipmentNameChanged, this, changed);
        connect(dye, &SettingsDye::activeBagChanged, this, changed);
        connect(dye, &SettingsDye::activeRecipeIdChanged, this, changed);
        connect(dye, &SettingsDye::dyeGrinderBrandChanged, this, changed);
        connect(dye, &SettingsDye::dyeGrinderModelChanged, this, changed);
        connect(dye, &SettingsDye::dyeGrinderSettingChanged, this, changed);
        connect(dye, &SettingsDye::dyeGrinderRpmChanged, this, changed);
        connect(network, &SettingsNetwork::recipeSortFieldChanged, this, changed);
        connect(network, &SettingsNetwork::recipeSortDirectionChanged, this, changed);
        connect(network, &SettingsNetwork::bagSortFieldChanged, this, changed);
        connect(network, &SettingsNetwork::bagSortDirectionChanged, this, changed);
        connect(graph, &SettingsGraph::snapshotChanged, this, changed);
        connect(brew, &SettingsBrew::brewOverridesChanged, this, changed);
        connect(m_settings->hardware(), &SettingsHardware::hotWaterFlowRateChanged,
                this, changed);
    }
    if (m_history) {
        connect(m_history, &ShotHistoryStorage::readyChanged, this, [this] {
            setHistoryReady(m_history && m_history->isReady());
        });
        connect(m_history, &ShotHistoryStorage::shotRequestReady, this,
                [this](const QString& id, qint64, const QVariantMap& result, const QString& reason) {
            finishHistory(id, QStringLiteral("shot"), reason.isEmpty(), result, reason);
        });
        connect(m_history, &ShotHistoryStorage::rankedProfilesRequestReady, this,
                [this](const QString& id, const QVariantMap& result, const QString& reason) {
            finishHistory(id, QStringLiteral("rankedProfilesForBean"), reason.isEmpty(), result, reason);
        });
        connect(m_history, &ShotHistoryStorage::latestShotForBeanProfileRequestReady, this,
                [this](const QString& id, const QVariantMap& result, const QString& reason) {
            finishHistory(id, QStringLiteral("latestShotForBeanProfile"), reason.isEmpty(), result, reason);
        });
        connect(m_history, &ShotHistoryStorage::latestGrindForBeanRequestReady, this,
                [this](const QString& id, const QVariantMap& result, const QString& reason) {
            finishHistory(id, QStringLiteral("latestGrindForBean"), reason.isEmpty(), result, reason);
        });
        connect(m_history, &ShotHistoryStorage::grindContextRequestReady, this,
                [this](const QString& id, const QVariantMap& result, const QString& reason) {
            finishGrindContext(id, result, reason);
        });
        connect(m_history, &ShotHistoryStorage::distinctBeanBrandsRequestReady, this,
                [this](const QString& id, const QStringList& values, const QString& reason) {
            finishHistory(id, QStringLiteral("distinctBeanBrands"), reason.isEmpty(),
                          {{QStringLiteral("values"), values}}, reason);
        });
        connect(m_history, &ShotHistoryStorage::distinctBeanTypesForBrandRequestReady, this,
                [this](const QString& id, const QStringList& values, const QString& reason) {
            finishHistory(id, QStringLiteral("distinctBeanTypesForBrand"), reason.isEmpty(),
                          {{QStringLiteral("values"), values}}, reason);
        });
        connect(m_history, &ShotHistoryStorage::shotMetadataUpdateRequestFinished, this,
                [this](const QString& id, qint64 shotId, bool success, const QString& reason) {
            finishHistory(id, QStringLiteral("updateShotMetadata"), success,
                          {{QStringLiteral("shotId"), shotId}}, reason);
        });
    }
    refresh();
    setHistoryReady(m_history && m_history->isReady());
}

void NativeRecipeContextSource::refresh()
{
    setReady(m_settings != nullptr);
    const QVariantMap preferences = capturePreferences();
    if (this->preferences() == preferences)
        return;
    setPreferences(preferences);
    setRevision(revision() + 1);
}

QVariantMap NativeRecipeContextSource::capturePreferences() const
{
    QVariantMap values;
    if (!m_settings)
        return values;
    const auto* app = m_settings->app();
    const auto* brew = m_settings->brew();
    const auto* dye = m_settings->dye();
    const auto* network = m_settings->network();
    values.insert(QStringLiteral("recipeSortField"), network->recipeSortField());
    values.insert(QStringLiteral("recipeSortDirection"), network->recipeSortDirection());
    values.insert(QStringLiteral("bagSortField"), network->bagSortField());
    values.insert(QStringLiteral("bagSortDirection"), network->bagSortDirection());
    values.insert(QStringLiteral("autoLoadRevertMinutes"), app->autoLoadRevertMinutes());
    values.insert(QStringLiteral("autoLoadRecipeId"), dye->autoLoadRecipeId());
    values.insert(QStringLiteral("grinderRpmCapable"),
                  dye->grinderRpmCapable(dye->dyeGrinderBrand(), dye->dyeGrinderModel()));
    values.insert(QStringLiteral("dyeBeanBrand"), dye->dyeBeanBrand());
    values.insert(QStringLiteral("dyeBeanType"), dye->dyeBeanType());
    values.insert(QStringLiteral("dyeBeanWeight"), dye->dyeBeanWeight());
    values.insert(QStringLiteral("dyeDrinkWeight"), dye->dyeDrinkWeight());
    values.insert(QStringLiteral("dyeBarista"), dye->dyeBarista());
    values.insert(QStringLiteral("dyeRoastDate"), dye->dyeRoastDate());
    values.insert(QStringLiteral("dyeRoastLevel"), dye->dyeRoastLevel());
    values.insert(QStringLiteral("dyeBeanBaseId"), dye->dyeBeanBaseId());
    values.insert(QStringLiteral("dyeBeanBaseData"), dye->dyeBeanBaseData());
    values.insert(QStringLiteral("activeBagFrozenDate"), dye->activeBagFrozenDate());
    values.insert(QStringLiteral("activeBagDefrostDate"), dye->activeBagDefrostDate());
    values.insert(QStringLiteral("activeBagOpenedDate"), dye->activeBagOpenedDate());
    values.insert(QStringLiteral("activeEquipmentId"), dye->activeEquipmentId());
    values.insert(QStringLiteral("activeBagId"), dye->activeBagId());
    values.insert(QStringLiteral("activeRecipeId"), dye->activeRecipeId());
    values.insert(QStringLiteral("dyeEquipmentName"), dye->dyeEquipmentName());
    values.insert(QStringLiteral("dyeGrinderBrand"), dye->dyeGrinderBrand());
    values.insert(QStringLiteral("dyeGrinderModel"), dye->dyeGrinderModel());
    values.insert(QStringLiteral("dyeGrinderRpm"), dye->dyeGrinderRpm());
    values.insert(QStringLiteral("dyeGrinderSetting"), dye->dyeGrinderSetting());
    values.insert(QStringLiteral("hasBrewYieldOverride"), brew->hasBrewYieldOverride());
    values.insert(QStringLiteral("brewYieldMode"), brew->brewYieldMode());
    values.insert(QStringLiteral("brewYieldOverride"), brew->brewYieldOverride());
    values.insert(QStringLiteral("minRatio"), brew->minRatio());
    values.insert(QStringLiteral("maxRatio"), brew->maxRatio());
    values.insert(QStringLiteral("lastUsedRatio"), brew->lastUsedRatio());
    values.insert(QStringLiteral("ratioPreset1"), brew->ratioPreset1());
    values.insert(QStringLiteral("ratioPreset2"), brew->ratioPreset2());
    values.insert(QStringLiteral("ratioPreset3"), brew->ratioPreset3());
    values.insert(QStringLiteral("selectedWaterVessel"), brew->selectedWaterVessel());
    values.insert(QStringLiteral("waterVesselPresets"), brew->waterVesselPresets());
    values.insert(QStringLiteral("waterTemperature"), brew->waterTemperature());
    values.insert(QStringLiteral("waterVolume"), brew->waterVolume());
    values.insert(QStringLiteral("waterVolumeMode"), brew->waterVolumeMode());
    values.insert(QStringLiteral("hotWaterFlowRate"), m_settings->hardware()->hotWaterFlowRate());
    values.insert(QStringLiteral("flushPresets"), brew->flushPresets());
    values.insert(QStringLiteral("selectedFlushPreset"), brew->selectedFlushPreset());
    values.insert(QStringLiteral("flushFlow"), brew->flushFlow());
    values.insert(QStringLiteral("flushSeconds"), brew->flushSeconds());
    values.insert(QStringLiteral("doseCupTareWeight"), brew->doseCupTareWeight());
    values.insert(QStringLiteral("doseCaptureSoundEnabled"), brew->doseCaptureSoundEnabled());
    values.insert(QStringLiteral("postShotReviewTimeout"), m_settings->value(QStringLiteral("postShotReviewTimeout"), 31));
    values.insert(QStringLiteral("espressoExtractionView"),
                  m_settings->value(QStringLiteral("espresso/extractionView"), QStringLiteral("chart")));
    values.insert(QStringLiteral("espressoShowPhaseIndicator"),
                  m_settings->value(QStringLiteral("espresso/showPhaseIndicator"), true).toBool());
    values.insert(QStringLiteral("espressoShowStats"),
                  m_settings->value(QStringLiteral("espresso/showStats"), true).toBool());
    const QVariantMap graph = m_settings->graph()->snapshot();
#define CAPTURE_GRAPH_TRACE(Getter, Setter, Key, Default, Signal) \
    values.insert(QStringLiteral(#Getter), graph.value(QStringLiteral(#Getter), Default));
    DECENZA_GRAPH_TRACES(CAPTURE_GRAPH_TRACE)
#undef CAPTURE_GRAPH_TRACE
    values.insert(QStringLiteral("rightAxisMode"), graph.value(QStringLiteral("rightAxisMode")));
    values.insert(QStringLiteral("phaseSummaryExpanded"),
                  m_settings->value(QStringLiteral("shotReview/phaseSummaryExpanded"), false).toBool());
    values.insert(QStringLiteral("comparisonGraphHeight"),
                  m_settings->value(QStringLiteral("comparison/graphHeight"), 0.0));
    values.insert(QStringLiteral("shotPage/graphHeight"), m_settings->value(QStringLiteral("shotPage/graphHeight"), 0.0));
    values.insert(QStringLiteral("postShotReview/graphHeight"), m_settings->value(QStringLiteral("postShotReview/graphHeight"), 0.0));
    return values;
}

bool NativeRecipeContextSource::validRequestId(const QString& requestId) const
{
    return isRequestId(requestId) && !m_pendingRequestIds.contains(requestId)
        && m_pendingRequestIds.size() < 64;
}

void NativeRecipeContextSource::setPreference(QString requestId, QString name,
                                               QVariant value, quint64 expectedRevision)
{
    const auto fail = [this, &requestId, &name](const QString& reason) {
        emit preferenceCompleted(requestId, name, false, preferences(), reason);
    };
    if (!validRequestId(requestId)) {
        fail(QStringLiteral("invalidRequest"));
        return;
    }
    if (!m_settings || !ready()) {
        fail(QStringLiteral("unavailable"));
        return;
    }
    if (expectedRevision != revision()) {
        refresh();
        fail(QStringLiteral("stalePreferences"));
        return;
    }

    QScopedValueRollback<bool> suppressRefresh(m_suppressPreferenceRefresh, true);
    bool accepted = false;
    if (name == QLatin1String("recipeSort") || name == QLatin1String("bagSort")) {
        QString field, direction;
        static const QSet<QString> recipeFields{
            QStringLiteral("dateUsed"), QStringLiteral("dateCreated"),
            QStringLiteral("coffee"), QStringLiteral("profile"), QStringLiteral("name")};
        static const QSet<QString> bagFields{
            QStringLiteral("dateUsed"), QStringLiteral("roastDate"),
            QStringLiteral("coffee"), QStringLiteral("roaster")};
        const bool recipeSort = name == QLatin1String("recipeSort");
        if (parseSortPreference(value, recipeSort ? recipeFields : bagFields,
                                &field, &direction)) {
            if (recipeSort) {
                m_settings->network()->setRecipeSortField(field);
                m_settings->network()->setRecipeSortDirection(direction);
            } else {
                m_settings->network()->setBagSortField(field);
                m_settings->network()->setBagSortDirection(direction);
            }
            accepted = true;
        }
    } else if (name == QLatin1String("recipeSortField")) {
        QString field;
        static const QSet<QString> fields{
            QStringLiteral("dateUsed"), QStringLiteral("dateCreated"),
            QStringLiteral("coffee"), QStringLiteral("profile"), QStringLiteral("name")};
        if (boundedString(value, &field, 32) && fields.contains(field)) {
            m_settings->network()->setRecipeSortField(field);
            accepted = true;
        }
    } else if (name == QLatin1String("recipeSortDirection")) {
        QString direction;
        if (boundedString(value, &direction, 4)
            && (direction == QLatin1String("ASC") || direction == QLatin1String("DESC"))) {
            m_settings->network()->setRecipeSortDirection(direction);
            accepted = true;
        }
    } else if (name == QLatin1String("autoLoadRevertMinutes")) {
        bool ok = false;
        const int minutes = value.toInt(&ok);
        if (ok && minutes >= 0 && minutes <= 60) {
            m_settings->app()->setAutoLoadRevertMinutes(minutes);
            accepted = true;
        }
    } else if (name == QLatin1String("autoLoadRecipeId")) {
        bool ok = false;
        const qlonglong id = value.toLongLong(&ok);
        if (ok && id >= -1 && id <= std::numeric_limits<int>::max()) {
            m_settings->dye()->setAutoLoadRecipeId(static_cast<int>(id));
            accepted = true;
        }
    } else if (name == QLatin1String("dyeBeanBrand")
               || name == QLatin1String("dyeBeanType")
               || name == QLatin1String("dyeRoastDate")
               || name == QLatin1String("dyeRoastLevel")
               || name == QLatin1String("dyeBarista")) {
        QString text;
        if (boundedString(value, &text, 128)) {
            if (name == QLatin1String("dyeBeanBrand")) m_settings->dye()->setDyeBeanBrand(text);
            else if (name == QLatin1String("dyeBeanType")) m_settings->dye()->setDyeBeanType(text);
            else if (name == QLatin1String("dyeRoastDate")) m_settings->dye()->setDyeRoastDate(text);
            else if (name == QLatin1String("dyeRoastLevel")) m_settings->dye()->setDyeRoastLevel(text);
            else m_settings->dye()->setDyeBarista(text);
            accepted = true;
        }
    } else if (name == QLatin1String("doseCupTareWeight")) {
        double weight = 0.0;
        if (finiteNumber(value, &weight) && weight >= 0.0 && weight <= 1000.0) {
            m_settings->brew()->setDoseCupTareWeight(weight);
            accepted = true;
        }
    } else if (name == QLatin1String("doseCaptureSoundEnabled")) {
        if (value.metaType().id() == QMetaType::Bool) {
            m_settings->brew()->setDoseCaptureSoundEnabled(value.toBool());
            accepted = true;
        }
    } else if (name == QLatin1String("espressoExtractionView")) {
        QString mode;
        if (boundedString(value, &mode, 16)
            && (mode == QLatin1String("chart") || mode == QLatin1String("cupFill"))) {
            m_settings->setValue(QStringLiteral("espresso/extractionView"), mode);
            accepted = true;
        }
    } else if (name == QLatin1String("espressoShowPhaseIndicator")) {
        if (value.metaType().id() == QMetaType::Bool) {
            m_settings->setValue(QStringLiteral("espresso/showPhaseIndicator"), value);
            accepted = true;
        }
    } else if (name == QLatin1String("espressoShowStats")) {
        if (value.metaType().id() == QMetaType::Bool) {
            m_settings->setValue(QStringLiteral("espresso/showStats"), value);
            accepted = true;
        }
    } else if (name == QLatin1String("rightAxisMode")) {
        QString mode;
        const QString current = m_settings->graph()->rightAxisMode();
        if (boundedString(value, &mode, 16)
            && mode == SettingsGraph::nextRightAxisMode(current)) {
            m_settings->graph()->setRightAxisMode(mode);
            accepted = true;
        }
#define SET_GRAPH_TRACE(Getter, Setter, Key, Default, Signal) \
    } else if (name == QLatin1String(#Getter)) { \
        if (value.metaType().id() == QMetaType::Bool) { \
            m_settings->graph()->Setter(value.toBool()); \
            accepted = true; \
        }
    DECENZA_GRAPH_TRACES(SET_GRAPH_TRACE)
#undef SET_GRAPH_TRACE
    } else if (name == QLatin1String("dyeBeanWeight")) {
        double weight = 0.0;
        if (finiteNumber(value, &weight) && weight >= 0.0 && weight <= 1000.0) {
            m_settings->dye()->setDyeBeanWeight(weight);
            accepted = true;
        }
    } else if (name == QLatin1String("dyeDrinkWeight")) {
        double weight = 0.0;
        if (finiteNumber(value, &weight) && weight >= 0.0 && weight <= 5000.0) {
            m_settings->dye()->setDyeDrinkWeight(weight);
            accepted = true;
        }
    } else if (name == QLatin1String("dyeBeanBaseId")) {
        QString id;
        if (boundedString(value, &id, 128)) {
            m_settings->dye()->setDyeBeanBaseId(id);
            accepted = true;
        }
    } else if (name == QLatin1String("dyeBeanBaseData")) {
        QString json;
        if (boundedString(value, &json, 64 * 1024)
            && (json.isEmpty() || QJsonDocument::fromJson(json.toUtf8()).isObject())) {
            m_settings->dye()->setDyeBeanBaseData(json);
            accepted = true;
        }
    } else if (name == QLatin1String("grindDialIn")) {
        if (value.metaType().id() == QMetaType::QVariantMap) {
            const QVariantMap fields = value.toMap();
            const bool hasSetting = fields.contains(QStringLiteral("dyeGrinderSetting"));
            const bool hasRpm = fields.contains(QStringLiteral("dyeGrinderRpm"));
            QString setting;
            int rpm = 0;
            const bool valid = !fields.isEmpty() && fields.size() <= 2
                && fields.size() == int(hasSetting) + int(hasRpm)
                && (!hasSetting || grinderSetting(fields.value(QStringLiteral("dyeGrinderSetting")), &setting))
                && (!hasRpm || grinderRpm(fields.value(QStringLiteral("dyeGrinderRpm")), &rpm))
                && (hasSetting || hasRpm);
            if (valid) {
                auto* dye = m_settings->dye();
                if (hasSetting)
                    dye->setDyeGrinderSetting(setting);
                if (hasRpm)
                    dye->setDyeGrinderRpm(rpm);
                accepted = true;
            }
        }
    } else if (name == QLatin1String("dyeGrinderSetting")) {
        QString text;
        if (grinderSetting(value, &text)) {
            m_settings->dye()->setDyeGrinderSetting(text);
            accepted = true;
        }
    } else if (name == QLatin1String("dyeGrinderRpm")) {
        int rpm = 0;
        if (grinderRpm(value, &rpm)) {
            m_settings->dye()->setDyeGrinderRpm(rpm);
            accepted = true;
        }
    } else if (name == QLatin1String("brewYieldAnchor")) {
        if (value.metaType().id() == QMetaType::QVariantMap) {
            const QVariantMap anchor = value.toMap();
            QString mode;
            double amount = 0.0;
            if (ServiceValue::hasExactKeys(anchor, {QStringLiteral("mode"), QStringLiteral("value")})
                && boundedString(anchor.value(QStringLiteral("mode")), &mode, 16)
                && (mode == QLatin1String("none") || mode == QLatin1String("absolute")
                    || mode == QLatin1String("ratio"))
                && finiteNumber(anchor.value(QStringLiteral("value")), &amount)
                && amount >= 0.0 && amount <= 500.0) {
                m_settings->brew()->setBrewYieldAnchor(amount, mode);
                accepted = true;
            }
        }
    } else if (name == QLatin1String("ratioPreset1")
               || name == QLatin1String("ratioPreset2")
               || name == QLatin1String("ratioPreset3")
               || name == QLatin1String("lastUsedRatio")) {
        double ratio = 0.0;
        auto* brew = m_settings->brew();
        if (finiteNumber(value, &ratio) && ratio >= brew->minRatio() && ratio <= brew->maxRatio()) {
            if (name == QLatin1String("ratioPreset1")) brew->setRatioPreset1(ratio);
            else if (name == QLatin1String("ratioPreset2")) brew->setRatioPreset2(ratio);
            else if (name == QLatin1String("ratioPreset3")) brew->setRatioPreset3(ratio);
            else brew->setLastUsedRatio(ratio);
            accepted = true;
        }
    } else if (name == QLatin1String("brewRatioAnchor")) {
        double ratio = 0.0;
        auto* brew = m_settings->brew();
        if (finiteNumber(value, &ratio) && ratio >= brew->minRatio() && ratio <= brew->maxRatio()) {
            brew->setLastUsedRatio(ratio);
            brew->setBrewRatioAnchor(ratio);
            accepted = true;
        }
    } else if (name == QLatin1String("selectedWaterVessel")) {
        bool ok = false;
        const int index = value.toInt(&ok);
        const auto count = m_settings->brew()->waterVesselPresets().size();
        if (ok && index >= 0 && index < count) {
            m_settings->brew()->setSelectedWaterCup(index);
            accepted = true;
        }
    } else if (name == QLatin1String("postShotReviewTimeout")) {
        bool ok = false;
        const int seconds = value.toInt(&ok);
        if (ok && seconds >= 0 && seconds <= 3600) {
            m_settings->setValue(name, seconds);
            accepted = true;
        }
    } else if (name == QLatin1String("phaseSummaryExpanded")) {
        if (value.metaType().id() == QMetaType::Bool) {
            m_settings->setValue(QStringLiteral("shotReview/phaseSummaryExpanded"), value);
            accepted = true;
        }
    } else if (name == QLatin1String("comparisonGraphHeight")) {
        double height = 0.0;
        if (finiteNumber(value, &height) && height >= 40.0 && height <= 4000.0) {
            m_settings->setValue(QStringLiteral("comparison/graphHeight"), height);
            accepted = true;
        }
    } else if (name == QLatin1String("shotPage/graphHeight")
               || name == QLatin1String("postShotReview/graphHeight")) {
        double height = 0.0;
        if (finiteNumber(value, &height) && height >= 40.0 && height <= 4000.0) {
            m_settings->setValue(name, height);
            accepted = true;
        }
    }

    if (!accepted) {
        fail(QStringLiteral("invalidPreference"));
        return;
    }
    refresh();
    emit preferenceCompleted(requestId, name, true, preferences(), {});
}

void NativeRecipeContextSource::executePresetEdit(QString requestId, QString domain,
                                                   QString operation, QVariantMap fields,
                                                   quint64 expectedRevision)
{
    bool registered = false;
    const auto finish = [this, &requestId, &domain, &operation, &registered](
                            bool success, const QString& reason) {
        if (registered)
            m_pendingRequestIds.remove(requestId);
        emit presetEditCompleted(requestId, domain, operation, success, revision(),
                                 preferences(), reason);
    };

    if (!validRequestId(requestId)) {
        finish(false, QStringLiteral("invalidRequest"));
        return;
    }
    if (!m_settings || !ready()) {
        finish(false, QStringLiteral("unavailable"));
        return;
    }
    if (expectedRevision != revision()) {
        refresh();
        finish(false, QStringLiteral("stalePreferences"));
        return;
    }

    auto* brew = m_settings->brew();
    QScopedValueRollback<bool> suppressRefresh(m_suppressPreferenceRefresh, true);
    bool accepted = false;
    if (domain == QLatin1String("waterVessel")) {
        const QVariantList presets = brew->waterVesselPresets();
        if (operation == QLatin1String("add")) {
            QString name, mode;
            int volume = 0, flowRate = 0;
            double temperature = 0.0;
            const bool valid = ServiceValue::hasExactKeys(fields, {QStringLiteral("name"), QStringLiteral("volume"),
                                                   QStringLiteral("mode"), QStringLiteral("flowRate"),
                                                   QStringLiteral("temperature")})
                && presetName(fields.value(QStringLiteral("name")), &name)
                && boundedString(fields.value(QStringLiteral("mode")), &mode, 16)
                && (mode == QLatin1String("weight") || mode == QLatin1String("volume"))
                && boundedInteger(fields.value(QStringLiteral("volume")), 10,
                                 mode == QLatin1String("volume") ? 255 : 500, &volume)
                && boundedInteger(fields.value(QStringLiteral("flowRate")), 5, 100, &flowRate)
                && finiteNumber(fields.value(QStringLiteral("temperature")), &temperature)
                && temperature >= 40.0 && temperature <= 100.0
                && !brew->waterVesselNameTaken(name);
            if (valid) {
                registered = true;
                m_pendingRequestIds.insert(requestId, QStringLiteral("presetEdit"));
                brew->addWaterVesselPreset(name, volume, mode, flowRate, temperature);
                accepted = brew->waterVesselPresets().size() == presets.size() + 1
                    && brew->getWaterVesselPreset(static_cast<int>(presets.size()))
                           .value(QStringLiteral("name")).toString() == name;
            }
        } else if (operation == QLatin1String("update")
                   || operation == QLatin1String("commit")) {
            int index = -1, volume = 0, flowRate = 0;
            QString expectedName, name, mode;
            double temperature = 0.0;
            const bool valid = ServiceValue::hasExactKeys(fields, {QStringLiteral("index"), QStringLiteral("expectedName"),
                                                   QStringLiteral("name"), QStringLiteral("volume"),
                                                   QStringLiteral("mode"), QStringLiteral("flowRate"),
                                                   QStringLiteral("temperature")})
                && expectedPreset(fields, presets, QStringLiteral("index"),
                                  QStringLiteral("expectedName"), &index)
                && presetName(fields.value(QStringLiteral("name")), &name)
                && boundedString(fields.value(QStringLiteral("mode")), &mode, 16)
                && (mode == QLatin1String("weight") || mode == QLatin1String("volume"))
                && boundedInteger(fields.value(QStringLiteral("volume")), 10,
                                 mode == QLatin1String("volume") ? 255 : 500, &volume)
                && boundedInteger(fields.value(QStringLiteral("flowRate")), 5, 100, &flowRate)
                && finiteNumber(fields.value(QStringLiteral("temperature")), &temperature)
                && temperature >= 40.0 && temperature <= 100.0
                && !brew->waterVesselNameTaken(name, index)
                && (operation != QLatin1String("commit")
                    || brew->selectedWaterVessel() == index);
            if (valid) {
                registered = true;
                m_pendingRequestIds.insert(requestId, QStringLiteral("presetEdit"));
                brew->updateWaterVesselPreset(index, name, volume, mode, flowRate, temperature);
                const QVariantMap updated = brew->getWaterVesselPreset(index);
                accepted = updated.value(QStringLiteral("name")).toString() == name
                    && updated.value(QStringLiteral("volume")).toInt() == volume
                    && updated.value(QStringLiteral("mode")).toString() == mode
                    && updated.value(QStringLiteral("flowRate")).toInt() == flowRate
                    && qFuzzyCompare(updated.value(QStringLiteral("temperature")).toDouble() + 1.0,
                                     temperature + 1.0);
                if (accepted && operation == QLatin1String("commit")) {
                    accepted = MachineControlPresets::selectWaterVessel(
                        brew, m_settings->hardware(), index,
                        [this] { emit presetApplyRequested(QStringLiteral("waterVessel")); });
                }
            }
        } else if (operation == QLatin1String("remove")) {
            int index = -1;
            const bool valid = ServiceValue::hasExactKeys(fields, {QStringLiteral("index"), QStringLiteral("expectedName")})
                && expectedPreset(fields, presets, QStringLiteral("index"),
                                  QStringLiteral("expectedName"), &index);
            if (valid) {
                registered = true;
                m_pendingRequestIds.insert(requestId, QStringLiteral("presetEdit"));
                brew->removeWaterVesselPreset(index);
                accepted = brew->waterVesselPresets().size() + 1 == presets.size();
            }
        } else if (operation == QLatin1String("move")) {
            int from = -1, to = -1;
            const bool valid = ServiceValue::hasExactKeys(fields, {QStringLiteral("fromIndex"),
                                                   QStringLiteral("expectedFromName"),
                                                   QStringLiteral("toIndex"),
                                                   QStringLiteral("expectedToName")})
                && expectedPreset(fields, presets, QStringLiteral("fromIndex"),
                                  QStringLiteral("expectedFromName"), &from)
                && expectedPreset(fields, presets, QStringLiteral("toIndex"),
                                  QStringLiteral("expectedToName"), &to)
                && from != to;
            if (valid) {
                QVariantList expected = presets;
                const QVariant item = expected.takeAt(from);
                expected.insert(to, item);
                registered = true;
                m_pendingRequestIds.insert(requestId, QStringLiteral("presetEdit"));
                brew->moveWaterVesselPreset(from, to);
                accepted = brew->waterVesselPresets() == expected;
            }
        }
    } else if (domain == QLatin1String("flush")) {
        const QVariantList presets = brew->flushPresets();
        if (operation == QLatin1String("add")) {
            QString name;
            double flow = 0.0, seconds = 0.0;
            const bool valid = ServiceValue::hasExactKeys(fields, {QStringLiteral("name"), QStringLiteral("flow"),
                                                   QStringLiteral("seconds")})
                && boundedString(fields.value(QStringLiteral("name")), &name, 128)
                && !name.isEmpty()
                && finiteNumber(fields.value(QStringLiteral("flow")), &flow)
                && flow >= 2.0 && flow <= 10.0
                && finiteNumber(fields.value(QStringLiteral("seconds")), &seconds)
                && seconds >= 1.0 && seconds <= 45.0;
            if (valid) {
                registered = true;
                m_pendingRequestIds.insert(requestId, QStringLiteral("presetEdit"));
                brew->addFlushPreset(name, flow, seconds);
                accepted = brew->flushPresets().size() == presets.size() + 1
                    && brew->getFlushPreset(static_cast<int>(presets.size()))
                           .value(QStringLiteral("name")).toString() == name;
            }
        } else if (operation == QLatin1String("update")
                   || operation == QLatin1String("commit")) {
            int index = -1;
            QString name;
            double flow = 0.0, seconds = 0.0;
            const bool valid = ServiceValue::hasExactKeys(fields, {QStringLiteral("index"),
                                                   QStringLiteral("expectedName"),
                                                   QStringLiteral("name"), QStringLiteral("flow"),
                                                   QStringLiteral("seconds")})
                && expectedPreset(fields, presets, QStringLiteral("index"),
                                  QStringLiteral("expectedName"), &index)
                && boundedString(fields.value(QStringLiteral("name")), &name, 128)
                && finiteNumber(fields.value(QStringLiteral("flow")), &flow)
                && flow >= 2.0 && flow <= 10.0
                && finiteNumber(fields.value(QStringLiteral("seconds")), &seconds)
                && seconds >= 1.0 && seconds <= 45.0
                && (operation != QLatin1String("commit")
                    || brew->selectedFlushPreset() == index);
            if (valid) {
                registered = true;
                m_pendingRequestIds.insert(requestId, QStringLiteral("presetEdit"));
                brew->updateFlushPreset(index, name, flow, seconds);
                const QVariantMap updated = brew->getFlushPreset(index);
                accepted = updated.value(QStringLiteral("name")).toString() == name
                    && qFuzzyCompare(updated.value(QStringLiteral("flow")).toDouble() + 1.0,
                                     flow + 1.0)
                    && qFuzzyCompare(updated.value(QStringLiteral("seconds")).toDouble() + 1.0,
                                     seconds + 1.0);
                if (accepted && operation == QLatin1String("commit")) {
                    accepted = MachineControlPresets::selectFlushPreset(
                        brew, index,
                        [this] { emit presetApplyRequested(QStringLiteral("flush")); });
                }
            }
        } else if (operation == QLatin1String("remove")) {
            int index = -1;
            const bool valid = ServiceValue::hasExactKeys(fields, {QStringLiteral("index"), QStringLiteral("expectedName")})
                && expectedPreset(fields, presets, QStringLiteral("index"),
                                  QStringLiteral("expectedName"), &index);
            if (valid) {
                registered = true;
                m_pendingRequestIds.insert(requestId, QStringLiteral("presetEdit"));
                brew->removeFlushPreset(index);
                accepted = brew->flushPresets().size() + 1 == presets.size();
            }
        } else if (operation == QLatin1String("move")) {
            int from = -1, to = -1;
            const bool valid = ServiceValue::hasExactKeys(fields, {QStringLiteral("fromIndex"),
                                                   QStringLiteral("expectedFromName"),
                                                   QStringLiteral("toIndex"),
                                                   QStringLiteral("expectedToName")})
                && expectedPreset(fields, presets, QStringLiteral("fromIndex"),
                                  QStringLiteral("expectedFromName"), &from)
                && expectedPreset(fields, presets, QStringLiteral("toIndex"),
                                  QStringLiteral("expectedToName"), &to)
                && from != to;
            if (valid) {
                QVariantList expected = presets;
                const QVariant item = expected.takeAt(from);
                expected.insert(to, item);
                registered = true;
                m_pendingRequestIds.insert(requestId, QStringLiteral("presetEdit"));
                brew->moveFlushPreset(from, to);
                accepted = brew->flushPresets() == expected;
            }
        }
    }

    if (!accepted) {
        refresh();
        finish(false, QStringLiteral("invalidPresetEdit"));
        return;
    }
    refresh();
    finish(true, {});
}

void NativeRecipeContextSource::applyShotBaseline(QString requestId, QVariantMap fields,
                                                  quint64 expectedRevision)
{
    auto fail = [this, &requestId](const QString& reason) {
        emit shotBaselineCompleted(requestId, false, revision(), preferences(), reason);
    };
    if (!validRequestId(requestId)) {
        fail(QStringLiteral("invalidRequest"));
        return;
    }
    if (!m_settings || !ready()) {
        fail(QStringLiteral("unavailable"));
        return;
    }
    if (expectedRevision != revision()) {
        refresh();
        fail(QStringLiteral("stalePreferences"));
        return;
    }

    static const QStringList keys{
        QStringLiteral("dyeBeanBrand"), QStringLiteral("dyeBeanType"),
        QStringLiteral("dyeRoastDate"), QStringLiteral("dyeRoastLevel"),
        QStringLiteral("dyeGrinderSetting"), QStringLiteral("dyeGrinderRpm"),
        QStringLiteral("activeEquipmentId"), QStringLiteral("shotEquipmentId"),
        QStringLiteral("dyeBarista"),
        QStringLiteral("dyeBeanWeight"), QStringLiteral("dyeDrinkWeight"),
        QStringLiteral("dyeBeanBaseId"), QStringLiteral("dyeBeanBaseData")};
    if (!ServiceValue::hasExactKeys(fields, keys)) {
        fail(QStringLiteral("invalidBaseline"));
        return;
    }

    QString beanBrand, beanType, roastDate, roastLevel, grinderSetting, barista;
    QString beanBaseId, beanBaseData;
    double beanWeight = 0.0, drinkWeight = 0.0;
    qint64 equipmentId = -1;
    double rpmNumber = 0.0;
    bool rpmOk = finiteNumber(fields.value(QStringLiteral("dyeGrinderRpm")), &rpmNumber)
        && std::floor(rpmNumber) == rpmNumber && rpmNumber >= 0.0 && rpmNumber <= 20000.0
        && fields.value(QStringLiteral("dyeGrinderRpm")).metaType().id() != QMetaType::Bool;
    const int rpm = rpmOk ? static_cast<int>(rpmNumber) : -1;
    const QVariant equipmentValue = fields.value(QStringLiteral("activeEquipmentId"));
    double equipmentNumber = 0.0;
    const bool equipmentOk = finiteNumber(equipmentValue, &equipmentNumber)
        && std::floor(equipmentNumber) == equipmentNumber
        && equipmentNumber >= -1.0 && equipmentNumber <= 9007199254740991.0
        && equipmentValue.metaType().id() != QMetaType::Bool;
    if (equipmentOk)
        equipmentId = static_cast<qint64>(equipmentNumber);
    const QVariant shotEquipmentValue = fields.value(QStringLiteral("shotEquipmentId"));
    double shotEquipmentNumber = 0.0;
    const bool shotEquipmentOk = finiteNumber(shotEquipmentValue, &shotEquipmentNumber)
        && std::floor(shotEquipmentNumber) == shotEquipmentNumber
        && shotEquipmentNumber >= -1.0 && shotEquipmentNumber <= 9007199254740991.0
        && shotEquipmentValue.metaType().id() != QMetaType::Bool;
    const qint64 shotEquipmentId = shotEquipmentOk
        ? static_cast<qint64>(shotEquipmentNumber) : -1;
    const bool valid = boundedString(fields.value(QStringLiteral("dyeBeanBrand")), &beanBrand, 128)
        && boundedString(fields.value(QStringLiteral("dyeBeanType")), &beanType, 128)
        && boundedString(fields.value(QStringLiteral("dyeRoastDate")), &roastDate, 128)
        && boundedString(fields.value(QStringLiteral("dyeRoastLevel")), &roastLevel, 128)
        && boundedString(fields.value(QStringLiteral("dyeGrinderSetting")), &grinderSetting, 128)
        && boundedString(fields.value(QStringLiteral("dyeBarista")), &barista, 128)
        && boundedString(fields.value(QStringLiteral("dyeBeanBaseId")), &beanBaseId, 128)
        && boundedString(fields.value(QStringLiteral("dyeBeanBaseData")), &beanBaseData, 64 * 1024)
        && (beanBaseData.isEmpty() || QJsonDocument::fromJson(beanBaseData.toUtf8()).isObject())
        && finiteNumber(fields.value(QStringLiteral("dyeBeanWeight")), &beanWeight)
        && beanWeight >= 0.0 && beanWeight <= 1000.0
        && finiteNumber(fields.value(QStringLiteral("dyeDrinkWeight")), &drinkWeight)
        && drinkWeight >= 0.0 && drinkWeight <= 5000.0
        && rpmOk && rpm >= 0 && rpm <= 20000
        && equipmentOk && (equipmentId == -1 || equipmentId > 0)
        && shotEquipmentOk && (shotEquipmentId == -1 || shotEquipmentId > 0);
    if (!valid) {
        fail(QStringLiteral("invalidBaseline"));
        return;
    }

    QScopedValueRollback<bool> suppressRefresh(m_suppressPreferenceRefresh, true);
    m_pendingRequestIds.insert(requestId, QStringLiteral("shotBaseline"));
    auto* dye = m_settings->dye();
    dye->setDyeBeanBrand(beanBrand);
    dye->setDyeBeanType(beanType);
    dye->setDyeRoastDate(roastDate);
    dye->setDyeRoastLevel(roastLevel);
    dye->setDyeBarista(barista);
    // Match the native page: only a positive equipment selection that differs
    // from the edited shot's stored equipment becomes the next-shot selection.
    if (equipmentId > 0 && equipmentId != shotEquipmentId)
        dye->setActiveEquipmentId(equipmentId);
    // Preserve the native rule: the last shot's grinder dial becomes sticky only
    // when it belongs to the active equipment (or the shot has no equipment).
    if (equipmentId <= 0 || dye->activeEquipmentId() == equipmentId) {
        dye->setDyeGrinderSetting(grinderSetting);
        if (rpm > 0)
            dye->setDyeGrinderRpm(rpm);
    }
    if (beanWeight > 0.0)
        dye->setDyeBeanWeight(beanWeight);
    if (drinkWeight > 0.0)
        dye->setDyeDrinkWeight(drinkWeight);
    dye->setDyeBeanBaseId(beanBaseId);
    dye->setDyeBeanBaseData(beanBaseData);
    refresh();
    m_pendingRequestIds.remove(requestId);
    emit shotBaselineCompleted(requestId, true, revision(), preferences(), {});
}

void NativeRecipeContextSource::requestHistory(QString requestId, QString operation,
                                               QVariantMap fields)
{
    const auto fail = [this, &requestId, &operation](const QString& reason) {
        emit historyCompleted(requestId, operation, false, {}, reason);
    };
    if (!validRequestId(requestId)) {
        fail(QStringLiteral("invalidRequest"));
        return;
    }
    if (!m_settings || (operation != QLatin1String("grindContext")
                        && (!m_history || !m_history->isReady()))) {
        fail(QStringLiteral("unavailable"));
        return;
    }
    m_pendingRequestIds.insert(requestId, operation);
    if (operation == QLatin1String("grindContext")) {
        QString brand, model, current;
        bool windowOk = false;
        const int windowSteps = fields.value(QStringLiteral("windowSteps")).toInt(&windowOk);
        if (!ServiceValue::hasExactKeys(fields, {QStringLiteral("brand"), QStringLiteral("model"),
                                QStringLiteral("current"), QStringLiteral("windowSteps")})
            || !boundedString(fields.value(QStringLiteral("brand")), &brand, 128)
            || !boundedString(fields.value(QStringLiteral("model")), &model, 128)
            || !boundedString(fields.value(QStringLiteral("current")), &current, 256)
            || !windowOk || windowSteps < 1 || windowSteps > 200) {
            m_pendingRequestIds.remove(requestId);
            fail(QStringLiteral("invalidFields"));
            return;
        }
        m_pendingGrindFields.insert(requestId, fields);
        const QString historyModel = model.isEmpty()
            ? m_settings->dye()->dyeGrinderModel() : model;
        if (m_history && m_history->isReady()) {
            m_history->requestGrindContextForRequest(requestId, historyModel);
        } else {
            finishGrindContext(requestId,
                {{QStringLiteral("grindStep"), 1.0},
                 {QStringLiteral("rpmStep"), 50.0},
                 {QStringLiteral("observedSettings"), QStringList{}}}, {});
        }
        return;
    }
    if (operation == QLatin1String("shot")) {
        if (!ServiceValue::hasExactKeys(fields, {QStringLiteral("shotId")})) {
            m_pendingRequestIds.remove(requestId);
            fail(QStringLiteral("invalidFields"));
            return;
        }
        bool ok = false;
        const qint64 shotId = fields.value(QStringLiteral("shotId")).toLongLong(&ok);
        if (!ok || shotId <= 0) {
            m_pendingRequestIds.remove(requestId);
            fail(QStringLiteral("invalidFields"));
            return;
        }
        m_history->requestShotForRequest(requestId, shotId);
        return;
    }

    if (operation == QLatin1String("distinctBeanBrands")) {
        if (!fields.isEmpty()) {
            m_pendingRequestIds.remove(requestId);
            fail(QStringLiteral("invalidFields"));
            return;
        }
        m_history->requestDistinctBeanBrandsForRequest(requestId);
        return;
    }
    if (operation == QLatin1String("distinctBeanTypesForBrand")) {
        QString brand;
        if (!ServiceValue::hasExactKeys(fields, {QStringLiteral("beanBrand")})
            || !boundedString(fields.value(QStringLiteral("beanBrand")), &brand, 128)) {
            m_pendingRequestIds.remove(requestId);
            fail(QStringLiteral("invalidFields"));
            return;
        }
        m_history->requestDistinctBeanTypesForBrandForRequest(requestId, brand);
        return;
    }

    auto stringField = [&fields](const QString& key, QString* output) {
        return fields.contains(key) && boundedString(fields.value(key), output, 128);
    };
    if (operation == QLatin1String("rankedProfilesForBean")) {
        QString brand, bean, roast, tea;
        if (!ServiceValue::hasExactKeys(fields, {QStringLiteral("beanBrand"), QStringLiteral("beanType"),
                                QStringLiteral("roastLevel"), QStringLiteral("teaType")})
            || !stringField(QStringLiteral("beanBrand"), &brand)
            || !stringField(QStringLiteral("beanType"), &bean)
            || !stringField(QStringLiteral("roastLevel"), &roast)
            || !stringField(QStringLiteral("teaType"), &tea)) {
            m_pendingRequestIds.remove(requestId);
            fail(QStringLiteral("invalidFields"));
            return;
        }
        m_history->requestRankedProfilesForBeanRequest(requestId, brand, bean, roast, tea);
        return;
    }
    if (operation == QLatin1String("latestShotForBeanProfile")) {
        QString brand, bean, profile;
        if (!ServiceValue::hasExactKeys(fields, {QStringLiteral("beanBrand"), QStringLiteral("beanType"),
                                QStringLiteral("profileName")})
            || !stringField(QStringLiteral("beanBrand"), &brand)
            || !stringField(QStringLiteral("beanType"), &bean)
            || !stringField(QStringLiteral("profileName"), &profile)) {
            m_pendingRequestIds.remove(requestId);
            fail(QStringLiteral("invalidFields"));
            return;
        }
        m_history->requestLatestShotForBeanProfileRequest(requestId, brand, bean, profile);
        return;
    }
    if (operation == QLatin1String("latestGrindForBean")) {
        QString brand, bean, roast;
        double equipmentNumber = 0;
        if (!ServiceValue::hasExactKeys(fields, {QStringLiteral("beanBrand"), QStringLiteral("beanType"),
                                QStringLiteral("roastLevel"), QStringLiteral("equipmentId")})
            || !stringField(QStringLiteral("beanBrand"), &brand)
            || !stringField(QStringLiteral("beanType"), &bean)
            || !stringField(QStringLiteral("roastLevel"), &roast)
            || !finiteNumber(fields.value(QStringLiteral("equipmentId")), &equipmentNumber)
            || std::floor(equipmentNumber) != equipmentNumber
            || equipmentNumber < -1 || equipmentNumber > 9007199254740991.0) {
            m_pendingRequestIds.remove(requestId);
            fail(QStringLiteral("invalidFields"));
            return;
        }
        m_history->requestLatestGrindForBeanRequest(
            requestId, brand, bean, roast, static_cast<qint64>(equipmentNumber));
        return;
    }
    m_pendingRequestIds.remove(requestId);
    fail(QStringLiteral("unsupportedOperation"));
}

void NativeRecipeContextSource::requestShotMetadataUpdate(QString requestId, qint64 shotId,
                                                          QVariantMap metadata)
{
    const auto fail = [this, &requestId](const QString& reason) {
        emit historyCompleted(requestId, QStringLiteral("updateShotMetadata"), false,
                              {}, reason);
    };
    if (!validRequestId(requestId)) {
        fail(QStringLiteral("invalidRequest"));
        return;
    }
    if (!m_history || !m_history->isReady()) {
        fail(QStringLiteral("unavailable"));
        return;
    }
    if (shotId <= 0 || !ShotHistoryStorage::validateShotMetadataRequest(metadata)) {
        fail(QStringLiteral("invalidFields"));
        return;
    }
    m_pendingRequestIds.insert(requestId, QStringLiteral("updateShotMetadata"));
    m_history->requestUpdateShotMetadataForRequest(requestId, shotId, metadata);
}

void NativeRecipeContextSource::finishHistory(const QString& requestId,
                                              const QString& operation,
                                              bool success,
                                              const QVariantMap& result,
                                              const QString& reason)
{
    if (m_pendingRequestIds.value(requestId) != operation)
        return;
    m_pendingRequestIds.remove(requestId);
    emit historyCompleted(requestId, operation, success, result, reason);
}

void NativeRecipeContextSource::finishGrindContext(const QString& requestId,
                                                   const QVariantMap& history,
                                                   const QString& reason)
{
    if (m_pendingRequestIds.value(requestId) != QLatin1String("grindContext"))
        return;
    const QVariantMap fields = m_pendingGrindFields.take(requestId);
    m_pendingRequestIds.remove(requestId);
    if (!reason.isEmpty() && reason != QLatin1String("notReady")
        && reason != QLatin1String("unavailable")) {
        emit historyCompleted(requestId, QStringLiteral("grindContext"), false, {}, reason);
        return;
    }

    const QString brand = fields.value(QStringLiteral("brand")).toString();
    const QString model = fields.value(QStringLiteral("model")).toString();
    const QString current = fields.value(QStringLiteral("current")).toString();
    const int windowSteps = fields.value(QStringLiteral("windowSteps")).toInt();
    QStringList observed = history.value(QStringLiteral("observedSettings")).toStringList();
    if (observed.size() > 8192) {
        emit historyCompleted(requestId, QStringLiteral("grindContext"), false, {},
                              QStringLiteral("tooManySettings"));
        return;
    }

    auto* dye = m_settings ? m_settings->dye() : nullptr;
    if (!dye) {
        emit historyCompleted(requestId, QStringLiteral("grindContext"), false, {},
                              QStringLiteral("unavailable"));
        return;
    }

    const double step = history.value(QStringLiteral("grindStep"), 1.0).toDouble();
    const double rpmStep = history.value(QStringLiteral("rpmStep"), 50.0).toDouble();
    const int decimals = grindDecimals(step, current);
    const QStringList currentCatalog = dye->stepGrinderSettingRange(
        brand, model, current, step, -windowSteps, windowSteps, decimals);

    QList<double> numericObserved;
    numericObserved.reserve(observed.size());
    for (const QString& value : observed) {
        bool ok = false;
        const double number = value.trimmed().toDouble(&ok);
        if (ok && std::isfinite(number))
            numericObserved.append(number);
    }
    std::sort(numericObserved.begin(), numericObserved.end());
    const QString medianAnchor = numericObserved.isEmpty()
        ? QString() : QString::number(numericObserved.at(numericObserved.size() / 2), 'g', 15);
    QStringList medianCatalog;
    if (current.isEmpty() && !medianAnchor.isEmpty()) {
        medianCatalog = dye->stepGrinderSettingRange(
            brand, model, medianAnchor, step, -windowSteps, windowSteps,
            grindDecimals(step, medianAnchor));
    }

    QVariantMap result;
    result.insert(QStringLiteral("brand"), brand);
    result.insert(QStringLiteral("model"), model);
    result.insert(QStringLiteral("current"), current);
    result.insert(QStringLiteral("grindStep"), step);
    result.insert(QStringLiteral("rpmStep"), rpmStep);
    result.insert(QStringLiteral("rpmCapable"), dye->grinderRpmCapable(brand, model));
    result.insert(QStringLiteral("clickIndexed"), dye->grinderIsClickIndexed(brand, model));
    result.insert(QStringLiteral("observedSettings"), observed);
    result.insert(QStringLiteral("medianAnchor"), medianAnchor);
    result.insert(QStringLiteral("currentCatalog"), currentCatalog);
    result.insert(QStringLiteral("medianAnchorCatalog"), medianCatalog);
    emit historyCompleted(requestId, QStringLiteral("grindContext"), true, result, {});
}
