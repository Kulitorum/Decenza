#pragma once

#include "uiworkstats.h"
#include <QObject>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

class UiWorkProbe : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit UiWorkProbe(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void begin() { m_timer.start(); }
    Q_INVOKABLE QString finish(const QString& value) {
        const bool changed = !m_hasValue || m_previous != value;
        m_previous = value;
        m_hasValue = true;
        ++m_calls;
        m_changes += changed;
        UiWorkStats::counter(UiWorkStats::Kind::WidgetFormat)
            .record(m_timer.nsecsElapsed(), changed, value.size());
        return value;
    }
    Q_INVOKABLE QVariantMap counts() const {
        return {{QStringLiteral("calls"), QVariant::fromValue(m_calls)},
                {QStringLiteral("changes"), QVariant::fromValue(m_changes)}};
    }
private:
    QElapsedTimer m_timer;
    QString m_previous;
    bool m_hasValue = false;
    quint64 m_calls = 0;
    quint64 m_changes = 0;
};
