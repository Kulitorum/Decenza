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
//   BEANBASE_LOG_*   qDebug   per-request detail
//   BEANBASE_INFO_*  qInfo    a stage that ran, or declined and why
//   BEANBASE_WARN_*  qWarning a failed fetch, a malformed reply, a corrupt blob
//
// INFO is the tier that matters: the recurring report is that nothing visibly
// happened, the answer is which stage declined, and the connections views
// filter at INFO — a DEBUG line never reaches the person asking.
//
// _TAGGED also emits logMessage and reaches the in-app view; _STDERR does not.
// There is deliberately no bare BEANBASE_LOG — see networklogging.h.

#define BEANBASE_LOG_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_BEANBASE, tag, msg, qDebug)
#define BEANBASE_INFO_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_BEANBASE, tag, msg, qInfo)
#define BEANBASE_WARN_TAGGED(tag, msg) \
    DECENZA_SUBSYS_LOG(DECENZA_LOG_MARKER_BEANBASE, tag, msg, qWarning)

// Stderr-only variants. BeanBaseClient has no logMessage signal, so these are
// the common case here.
#define BEANBASE_LOG_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_BEANBASE, tag, msg, qDebug)
#define BEANBASE_INFO_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_BEANBASE, tag, msg, qInfo)
#define BEANBASE_WARN_STDERR(tag, msg) \
    DECENZA_SUBSYS_LOG_STDERR(DECENZA_LOG_MARKER_BEANBASE, tag, msg, qWarning)
