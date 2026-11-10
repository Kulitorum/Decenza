# Spec Delta

## MODIFIED Requirements

### Requirement: Reconnect on transient drop is owned by the app-level reconnect loop

The WiFi driver SHALL NOT schedule reconnect attempts internally. On WebSocket disconnect, or a transient connect failure before the connected state, it SHALL call setConnected(false) and return, leaving retry to the app-level reconnect loop in main.cpp. After a transient transport failure the next attempt SHALL re-resolve the hostname; if resolution fails, it SHALL still dial the remembered address.

#### Scenario: WebSocket disconnects mid-stream
- **WHEN** the WiFi WebSocket emits `disconnected` after having been connected
- **THEN** the driver propagates the disconnect via `setConnected(false)` and does NOT schedule its own reconnect; the app-level `scaleReconnectTimer` schedules the retry

#### Scenario: Connect attempt fails transiently before connecting
- **WHEN** a connect attempt fails with a transient socket-layer error before the WebSocket upgrade completes
- **THEN** the driver ends the attempt without an internal retry, and the app-level `scaleReconnectTimer` schedules the next attempt

#### Scenario: Retry re-resolves the hostname
- **WHEN** the app-level reconnect loop retries a saved `.local` WiFi scale hostname after a transient failure
- **THEN** an mDNS resolution for that hostname is performed as part of the attempt, and the freshly resolved address is the one dialled

#### Scenario: Scale moved to a new address and the old one went dark
- **WHEN** DHCP has reassigned the scale to a different address, the address it vacated answers nothing, and the driver holds the vacated address in its cache
- **THEN** the failure classifies as transient and the cache is retained, and the following attempt re-resolves and dials the scale's new address rather than re-dialling the dead one indefinitely

#### Scenario: Resolution fails on the retry — dial the remembered address anyway
- **WHEN** the retry's hostname resolution fails (an unreliable or unavailable mDNS responder) and a cached IP is held for that hostname
- **THEN** the driver dials the cached IP rather than ending the attempt without opening a socket, so a connection is attempted on every cycle

#### Scenario: Reconnect also fails
- **WHEN** the app-level reconnect re-fires `connectToHost()` and that attempt also disconnects
- **THEN** the driver again propagates `setConnected(false)`; the app-level retry loop starts the next attempt immediately when no operation is running, and otherwise applies its backoff schedule (5 s / 30 s / 60 s)

## ADDED Requirements

### Requirement: A missing saved scale is retried without slowing down

While a saved scale is disconnected the app SHALL keep retrying for as long as the screensaver is off. Outside an operation, with Bluetooth available and the app not suspended, each failed attempt SHALL start the next one immediately. During an operation the 5 s / 30 s / 60 s schedule SHALL apply. The schedule SHALL NOT slow beyond 60 s. Each change of cadence SHALL be logged at INFO.

#### Scenario: Scale switched on while the machine is idle
- **WHEN** the saved scale has been off for several minutes with the machine idle and is switched on
- **THEN** it is connected within one attempt, with no wait between attempts

#### Scenario: Scale missing during a shot
- **WHEN** an attempt fails while a shot is running
- **THEN** the next attempt waits out the backoff schedule

#### Scenario: Screensaver
- **WHEN** the screensaver is showing
- **THEN** no attempt is made, and closing it restarts the schedule

### Requirement: WiFi-to-Bluetooth fallback takes only a saved scale

When the saved WiFi scale cannot be reached, the app SHALL fall back only to a Bluetooth scale that is in the saved-scales list. A scale never connected, or forgotten, SHALL NOT be connected or saved, and the log SHALL say at INFO that it was seen and declined. With no Bluetooth scale saved, the fallback scan SHALL NOT run.

#### Scenario: Forgotten scale in range
- **WHEN** the WiFi scale is unreachable and a Decent Bluetooth scale the user forgot is advertising
- **THEN** it is not connected and does not reappear in the saved list

#### Scenario: Saved Bluetooth backup
- **WHEN** the WiFi scale is unreachable and a saved Decent Bluetooth scale is advertising
- **THEN** it is connected and the WiFi scale stays the primary

#### Scenario: Only the WiFi scale is saved
- **WHEN** the WiFi scale is unreachable and no Bluetooth scale is saved
- **THEN** no fallback scan runs and the next WiFi attempt follows directly
