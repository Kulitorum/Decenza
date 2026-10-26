#pragma once

#include <QByteArray>
#include <QString>

struct ShotRecord;

namespace ShotFileExport {
enum class Format { Shot, Profile, DebugLog };

// Uses the recorded profile and the canonical Visualizer-format serializer.
QByteArray data(const ShotRecord& record, Format format, QString* reason);
}
