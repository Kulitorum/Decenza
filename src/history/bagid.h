#pragma once

#include <QtGlobal>

// Canonical coffee-bag-id sentinel — the SINGLE definition of the "is this a
// real coffee_bags row reference?" threshold.
//
// A bag id is a coffee_bags row id. The sentinel for "no bag" is bagId <= 0
// (or a NULL shots.bag_id column, which the DB read maps to -1); the struct
// default is -1. A positive id references a real bag. This predicate is the
// read side — it accepts the whole "no bag" class; -1 is the canonical value
// to WRITE for "none" (struct defaults, the "clear selection" path).
//
// This one predicate is shared by every site that asks the question: the
// per-shot snapshot fields (ShotRecord / ShotSaveData / ShotMetadata /
// ShotProjection — the last exposes it to QML as ShotProjection::hasBag()),
// the active-bag selection (SettingsDye), bag-id tool inputs (MCP), and the
// search-model tier invariant. Call bagIdIsSet() instead of hand-rolling
// > 0 / != -1 so the boundary can't drift — a stray >= 0 would treat the
// phantom id 0 as a real bag and leak it into uploads.
constexpr bool bagIdIsSet(qint64 bagId) { return bagId > 0; }
