#pragma once

#include <QVariantList>
#include <QVariantMap>

// Shared nearest-sample readout used by the native model and its paired
// QtRO facade. Each entry in `series` is a QVariantList of QPointF samples in
// this fixed order: pressure, flow, temperature, weight, weight flow, resistance,
// conductance, conductance derivative, Darcy resistance, mix temperature,
// mix-temperature goal.
namespace ShotComparisonReadout {
QVariantMap valuesAtTime(const QVariantList& series, double time);
}
