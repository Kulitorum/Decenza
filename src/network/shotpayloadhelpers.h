#pragma once

// Payload helpers shared by the Visualizer and Decent shot uploaders, so the
// two serializers resample and name things identically.

#include "profile/profile.h"

#include <QDir>
#include <QJsonArray>
#include <QPointF>
#include <QStandardPaths>
#include <QString>
#include <QVector>

// Where an uploader writes its last request/response for inspection
// (last_upload.json, last_decent_upload.json): Documents, else app data.
inline QString uploadDebugFilePath(const QString& fileName) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + fileName;
}

// Why a shot is not uploaded, by the policy every destination shares: no
// maintenance cycles (see Profile::isMaintenanceBeverageType), nothing shorter
// than the shared minimum length (SettingsUpload::minDuration).
enum class UploadIneligible { None, Maintenance, TooShort };
inline UploadIneligible uploadIneligibility(const QString& beverageType, double durationSec, double minDurationSec) {
    if (Profile::isMaintenanceBeverageType(beverageType)) return UploadIneligible::Maintenance;
    if (durationSec < minDurationSec) return UploadIneligible::TooShort;
    return UploadIneligible::None;
}

// Brand and model as one name ("Niche Zero"), or whichever is set.
inline QString grinderDisplayName(const QString& brand, const QString& model) {
    if (brand.isEmpty()) return model;
    if (model.isEmpty()) return brand;
    return brand + QLatin1Char(' ') + model;
}

// Helper: Interpolate goal data to match elapsed timestamps
// Goal data may have different timestamps or gaps; we need to align to the master elapsed array
// Gaps > 0.5s between goal points indicate mode switches (flow/pressure) - return 0 during gaps
inline QJsonArray interpolateGoalData(const QVector<QPointF>& goalData, const QVector<QPointF>& masterData) {
    QJsonArray result;

    if (goalData.isEmpty() || masterData.isEmpty()) {
        // Return zeros for all timestamps if no goal data
        for (qsizetype i = 0; i < masterData.size(); ++i) {
            result.append(0.0);
        }
        return result;
    }

    // Gap threshold: if consecutive goal points are more than 0.5s apart, treat as a gap
    constexpr double GAP_THRESHOLD = 0.5;

    qsizetype goalIdx = 0;
    for (const auto& masterPt : masterData) {
        double t = masterPt.x();

        // Find the goal data points surrounding this timestamp
        while (goalIdx < goalData.size() - 1 && goalData[goalIdx + 1].x() <= t) {
            goalIdx++;
        }

        if (goalIdx == 0 && t < goalData[0].x()) {
            // Before first goal point - use 0
            result.append(0.0);
        } else if (goalIdx >= goalData.size() - 1) {
            // At or past last point
            double timeSinceLast = t - goalData.last().x();
            if (timeSinceLast > GAP_THRESHOLD) {
                // Far past the last goal point - probably in a different mode
                result.append(0.0);
            } else {
                result.append(goalData.last().y());
            }
        } else {
            // Between goalData[goalIdx] and goalData[goalIdx+1]
            double t0 = goalData[goalIdx].x();
            double t1 = goalData[goalIdx + 1].x();
            double v0 = goalData[goalIdx].y();
            double v1 = goalData[goalIdx + 1].y();

            // Check for gap between goal points
            if (t1 - t0 > GAP_THRESHOLD) {
                // Gap detected - check which side of the gap we're on
                if (t - t0 < GAP_THRESHOLD) {
                    // Close to the earlier point - use its value
                    result.append(v0);
                } else if (t1 - t < GAP_THRESHOLD) {
                    // Close to the later point - use its value
                    result.append(v1);
                } else {
                    // In the middle of the gap - return 0
                    result.append(0.0);
                }
            } else if (t1 - t0 > 0.001) {
                // Normal case - interpolate
                double ratio = (t - t0) / (t1 - t0);
                result.append(v0 + ratio * (v1 - v0));
            } else {
                result.append(v0);
            }
        }
    }

    return result;
}
