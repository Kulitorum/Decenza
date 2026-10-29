#pragma once

#include "controllers/profilemanager.h"
#include "core/yieldspec.h"

#include <QHash>
#include <cmath>
#include <optional>
#include <utility>

// The setup used to decide whether a review may offer changes. It is separate
// from the shot record, which can be edited and can be assembled after the pour.
struct ShotReviewSetup {
    BrewSettingsSnapshot brew;
    QString beanBrand;
    QString beanType;
    QString roastDate;
    QString roastLevel;
    QString beanBaseId;
    QString beanBaseData;
    QString barista;
    double targetG = 0;

    bool matches(const ShotReviewSetup& other) const {
        const auto sameId = [](qint64 a, qint64 b) { return (a > 0 ? a : 0) == (b > 0 ? b : 0); };
        const auto sameNumber = [](double a, double b) {
            return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < 0.005;
        };
        const QString mode = YieldSpec::normalizedMode(brew.yieldMode);
        const QString otherMode = YieldSpec::normalizedMode(other.brew.yieldMode);
        return sameId(brew.activeBagId, other.brew.activeBagId)
            && sameId(brew.activeRecipeId, other.brew.activeRecipeId)
            && sameId(brew.activeEquipmentId, other.brew.activeEquipmentId)
            && brew.profileTitle == other.brew.profileTitle
            && brew.baseProfileName == other.brew.baseProfileName
            && brew.profileContentSha256 == other.brew.profileContentSha256
            && beanBrand == other.beanBrand && beanType == other.beanType
            && roastDate == other.roastDate && roastLevel == other.roastLevel
            && beanBaseId == other.beanBaseId && beanBaseData == other.beanBaseData
            && barista == other.barista
            && sameNumber(brew.doseG, other.brew.doseG)
            && brew.grindSetting == other.brew.grindSetting && brew.grindRpm == other.brew.grindRpm
            && mode == otherMode
            && (mode == YieldSpec::modeNone() || sameNumber(brew.yieldValue, other.brew.yieldValue))
            && sameNumber(targetG, other.targetG)
            && sameNumber(brew.hasTemperatureOverride ? brew.temperatureC : brew.profileTemperatureC,
                          other.brew.hasTemperatureOverride ? other.brew.temperatureC : other.brew.profileTemperatureC);
    }
};

// Operation IDs keep an asynchronously saved shot paired with its own start,
// even if another cycle starts before the save callback runs.
class ShotReviewBaselines {
public:
    explicit ShotReviewBaselines(qint64 startupShotId = 0) : m_startupShotId(startupShotId) {}
    void started(qint64 operationId, const ShotReviewSetup& setup) {
        // A cycle that never extracts may never produce a matching completion.
        // Bound retained snapshots; a missing old snapshot fails closed.
        if (m_started.size() >= 64) m_started.clear();
        m_started.insert(operationId, setup);
    }
    std::optional<ShotReviewSetup> take(qint64 operationId) {
        const auto it = m_started.find(operationId);
        if (it == m_started.end()) return std::nullopt;
        const ShotReviewSetup result = it.value();
        m_started.erase(it);
        return result;
    }
    void saved(qint64 shotId, std::optional<ShotReviewSetup> setup) {
        m_savedShotId = shotId;
        m_saved = std::move(setup);
        m_startupShotId = 0;
    }
    std::optional<ShotReviewSetup> forReview(qint64 shotId, const ShotReviewSetup& current) const {
        if (shotId <= 0) return std::nullopt;
        if (shotId == m_savedShotId) return m_saved;
        if (shotId == m_startupShotId) return current;
        return std::nullopt;
    }
private:
    qint64 m_startupShotId = 0;
    qint64 m_savedShotId = 0;
    QHash<qint64, ShotReviewSetup> m_started;
    std::optional<ShotReviewSetup> m_saved;
};
