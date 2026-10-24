#pragma once

#include <QDateTime>
#include <QString>
#include <atomic>
#include <memory>

// Canonical host media preparation; no network, settings or QObject ownership.
namespace PersonalMediaPreparation {
inline constexpr qint64 MaximumFileBytes = 500 * 1024 * 1024;
bool resizeImage(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight);
bool resizeVideo(const QString& inputPath, const QString& outputPath, int maxWidth, int maxHeight,
                 const std::shared_ptr<std::atomic_bool>& cancelled = {});
QDateTime extractImageDate(const QString& imagePath, const std::shared_ptr<std::atomic_bool>& cancelled = {});
QDateTime extractVideoDate(const QString& videoPath, const std::shared_ptr<std::atomic_bool>& cancelled = {});

struct Result {
    QString path;
    QDateTime mediaDate;
    QString reason;
};
// outputDirectory is a private worker-owned stage on the personal-media volume.
Result prepare(const QString& inputPath, const QString& originalName,
               const QString& outputDirectory, const std::shared_ptr<std::atomic_bool>& cancelled);
} // namespace PersonalMediaPreparation
