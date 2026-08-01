#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for storage durability: whether an edit the user made
// actually reached the database.
//
// Every line gets the [Storage] marker from the registry (core/logtags.h) plus a
// tag naming its own source:
//
//     [Storage][Drain] nothing queued at exit
//     [Storage][Drain] drained before backgrounding
//     [Storage][Worker] "CoffeeBagStorageWorker" destroyed with 2 DB task(s) still queued - those writes are being discarded
//     [Storage][Export] shot export threads still running at exit - exported JSON for a just-edited shot may be stale
//
// Those are copied verbatim from the format strings, so a grep for a phrase here
// finds the real line. Keep them that way when the wording changes.
//
// ---- Why this subsystem exists ------------------------------------------
//
// Because "I rated that shot and it came back blank" and "my note vanished" are
// the same event, and neither was answerable from a submitted log. Writes are
// queued to a per-storage background worker; a worker destroyed with tasks still
// queued discards them. Nothing in production waited for that until the drain
// landed, and the drain's own quiet path — nothing queued, the common outcome —
// logged nothing at all, which made "it ran and found nothing" indistinguishable
// from "it never ran". That cost a wrong reading of a real device log within an
// hour of shipping.
//
// The narrative also spans three files (the drain in maincontroller.cpp, the
// discard in dbutils.h, the export wait in main.cpp), so before this marker no
// single grep returned it.
//
// Not filed under [Equipment]: that answers what gear a shot was pulled on and
// why identity forked. This answers whether ANY write survived, for shots, bags,
// recipes and equipment alike — a different question, and filing it under one of
// its four subjects would send a reader hunting in the wrong place (see
// LOGGING.md, "Don't let one subsystem claim a shared resource").
//
// ---- Choosing a helper --------------------------------------------------
//
//   STORAGE_LOG_*   qDebug   the ordinary outcome, including "nothing to do"
//   STORAGE_INFO_*  qInfo    reserved; nothing here is part of a user's narrative
//                            unless it went wrong
//   STORAGE_WARN_*  qWarning work was actually discarded, or could not be waited for
//
// The suffix is the other half of the choice and is NOT cosmetic: _TAGGED also
// emits logMessage and so reaches the in-app view, _STDERR does not. Every site
// here is either a controller with no logMessage signal, a header-only worker
// destructor, or main()'s shutdown handler — so _STDERR is the case in practice,
// the same reason the SAW and Equipment helpers lean that way. Both are spelled
// out; there is deliberately no bare STORAGE_LOG (see networklogging.h for why an
// unsuffixed name is a trap across headers).

#define STORAGE_LOG_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_STORAGE, tag, msg, qDebug)
#define STORAGE_INFO_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_STORAGE, tag, msg, qInfo)
#define STORAGE_WARN_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_STORAGE, tag, msg, qWarning)

#define STORAGE_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_STORAGE, tag, msg, qDebug)
#define STORAGE_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_STORAGE, tag, msg, qInfo)
#define STORAGE_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_STORAGE, tag, msg, qWarning)
