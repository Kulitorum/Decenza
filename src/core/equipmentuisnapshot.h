#pragma once

#include <QVariantMap>

class SettingsDye;
class ShotHistoryStorage;

// The native dialog, HTTP route, and QtRO service share these small projections
// so registry choices and create defaults have one source of truth.
namespace EquipmentUiSnapshot {
QVariantMap createSeed(const SettingsDye* dye);
QVariantMap options(const SettingsDye* dye, ShotHistoryStorage* history);
}
