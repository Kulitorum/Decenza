#pragma once

#include <QObject>
#include <QTimer>

// Shared with event-replay tests so recovery is exercised without starting
// platform discovery. The timer belongs to the app's existing retry ladder.
template <typename Discovery, typename Paired, typename OnArmed>
void connectRefractometerRecovery(Discovery* discovery, QObject* context,
                                 QTimer* timer, int& attempt, int firstDelayMs,
                                 Paired paired, OnArmed onArmed)
{
    const auto refresh = [discovery, timer, &attempt, firstDelayMs, paired, onArmed] {
        if (!paired() || !discovery->isRefractometerHunt()
            || discovery->isRefractometerConnected() || discovery->isDisabled()
            || discovery->firmwareUpdateActive()) {
            timer->stop();
            attempt = 0;
            return;
        }
        if (timer->isActive()) return;
        attempt = 0;
        timer->start(firstDelayMs);
        onArmed();
    };
    QObject::connect(discovery, &Discovery::refractometerConnectedChanged, context, refresh);
    QObject::connect(discovery, &Discovery::refractometerHuntChanged, context, refresh);
    QObject::connect(discovery, &Discovery::disabledChanged, context, refresh);
    QObject::connect(discovery, &Discovery::firmwareUpdateActiveChanged, context, refresh);
}
