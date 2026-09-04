#pragma once

#include "core/logtags.h"

#include <QDebug>
#include <QString>

// Shared logging helpers for the coffee-bag detail pipeline: the product URL's
// state, archive recovery, the AI product-page search, and page extraction.
//
// Every line gets the [BeanBase] marker from the registry (core/logtags.h) plus
// a tag naming its own stage:
//
//     [BeanBase][Link] Stored URL is gone (404), asking the archive
//     [BeanBase][Extract] Page unreadable - no archived copy
//
// One subsystem, not three, because they are one question asked at different
// stages: "there is no picture on this bag" and "Get info did nothing" are
// almost always the same dead URL seen from two screens.
//
//   BEANBASE_INFO_STDERR  qInfo     a stage that ran, or declined and why
//   BEANBASE_WARN_STDERR  qWarning  a failed fetch, a malformed reply, a corrupt blob
//
// INFO is the tier that matters here: the recurring report is that nothing
// visibly happened, and the answer is which stage declined.
//
// Stderr-only: neither BeanBaseClient nor AIManager has a logMessage signal, so
// the _TAGGED variants the other subsystems carry would have no caller. Add one
// when a line needs the in-app view; see networklogging.h for why there is no
// unsuffixed name.

#define BEANBASE_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_BEANBASE, tag, msg, qInfo)
#define BEANBASE_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_BEANBASE, tag, msg, qWarning)
