#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for equipment packages: the grinder, basket and puck
// prep a shot was pulled on.
//
// Every line gets the [Equipment] marker from the registry (core/logtags.h) plus
// a tag naming its own source:
//
//     [Equipment][Identity] package 4 edit applied in place (enrichment - filled in a component that was empty)
//     [Equipment][Merge] package 7 folded into 4 and deleted - 213 shots, 2 bags, 1 recipes moved
//     [Equipment][Migration] 35 complete - merged 1 package(s) that a burr edit had split off
//
// Those are copied verbatim from the format strings, so a grep for a phrase here
// finds the real line. Keep them that way when the wording changes.
//
// ---- Why this subsystem exists ------------------------------------------
//
// Because "the app thinks I have a new grinder" and "my grind history vanished"
// are the same event seen from two sides, and neither was answerable from a
// submitted log. Equipment identity is copy-on-write: an edit either applies in
// place, forks a NEW package and retires the old one, or merges into an existing
// one — and a fork is precisely what detaches the shot history, because the
// grinder calibration matches shots on model AND burrs while dial-in grouping
// keys on equipment_id. #1713 was exactly that, and the log said nothing at all
// about which branch ran. The line naming the decision is the whole point of the
// marker; everything else here is context for it.
//
// Not filed under a device marker on purpose: no equipment package owns a
// transport, and a reader sent to [Scale] or [DE1] for a package-identity
// question is looking in the wrong place entirely (see LOGGING.md's "the test is
// the question, not the hardware").
//
// ---- Choosing a helper --------------------------------------------------
//
//   EQUIP_LOG_*   qDebug   per-field detail, scan bookkeeping
//   EQUIP_INFO_*  qInfo    the decision itself, a merge, a migration outcome —
//                          the narrative a user (or their AI) reads back
//   EQUIP_WARN_*  qWarning a refused merge, a failed heal, an SQL failure
//
// The suffix is the other half of the choice and is NOT cosmetic: _TAGGED also
// emits logMessage and so reaches the in-app view, _STDERR does not. Equipment
// writes run on storage worker threads and inside migrations, neither of which
// carries a logMessage signal, so _STDERR is the common case here — the same
// reason the SAW and Theme helpers lean that way. Both are spelled out; there is
// deliberately no bare EQUIP_LOG (see networklogging.h for why an unsuffixed name
// is a trap across headers).

#define EQUIP_LOG_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_EQUIPMENT, tag, msg, qDebug)
#define EQUIP_INFO_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_EQUIPMENT, tag, msg, qInfo)
#define EQUIP_WARN_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_EQUIPMENT, tag, msg, qWarning)

#define EQUIP_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_EQUIPMENT, tag, msg, qDebug)
#define EQUIP_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_EQUIPMENT, tag, msg, qInfo)
#define EQUIP_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_EQUIPMENT, tag, msg, qWarning)
