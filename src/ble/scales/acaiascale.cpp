#include "acaiascale.h"
#include "../protocol/de1characteristics.h"
#include "scalelogging.h"
#include <cmath>

#define ACAIA_LOG(msg)  SCALE_LOG("AcaiaScale", msg)
#define ACAIA_INFO(msg) SCALE_INFO("AcaiaScale", msg)
#define ACAIA_WARN(msg) SCALE_WARN("AcaiaScale", msg)

AcaiaScale::AcaiaScale(ScaleBleTransport* transport, QObject* parent)
    : ScaleDevice(parent)
    , m_transport(transport)
{
    if (m_transport) {
        m_transport->setParent(this);

        connect(m_transport, &ScaleBleTransport::connected,
                this, &AcaiaScale::onTransportConnected);
        connect(m_transport, &ScaleBleTransport::disconnected,
                this, &AcaiaScale::onTransportDisconnected);
        connect(m_transport, &ScaleBleTransport::error,
                this, &AcaiaScale::onTransportError);
        connect(m_transport, &ScaleBleTransport::serviceDiscovered,
                this, &AcaiaScale::onServiceDiscovered);
        connect(m_transport, &ScaleBleTransport::servicesDiscoveryFinished,
                this, &AcaiaScale::onServicesDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicsDiscoveryFinished,
                this, &AcaiaScale::onCharacteristicsDiscoveryFinished);
        connect(m_transport, &ScaleBleTransport::characteristicChanged,
                this, &AcaiaScale::onCharacteristicChanged);
        // Forward transport logs to scale log
        connect(m_transport, &ScaleBleTransport::logMessage,
                this, &ScaleDevice::logMessage);
    }

    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &AcaiaScale::sendHeartbeat);

    m_initTimer = new QTimer(this);
    connect(m_initTimer, &QTimer::timeout, this, &AcaiaScale::onInitTimer);
}

AcaiaScale::~AcaiaScale() {
    stopAllTimers();
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
}

void AcaiaScale::stopAllTimers() {
    m_heartbeatTimer->stop();
    m_initTimer->stop();
}

void AcaiaScale::connectToDevice(const QBluetoothDeviceInfo& device) {
    if (!m_transport) {
        ACAIA_WARN("connectToDevice called with no transport");
        return;
    }

    // Don't tear down an established link. A stray scaleDiscovered for a scale
    // we're already talking to would otherwise run the reset below on a live
    // connection — stopping the heartbeat and clearing m_characteristicsReady,
    // which blocks every subsequent write.
    //
    // Note what this deliberately does NOT guard: a duplicate connect while the
    // link is still coming up. QtScaleBleTransport::connectToDevice debounces
    // that on the controller's live state, and unlike a flag here it recovers
    // once the controller is gone. (CoreBluetoothScaleBleTransport has no such
    // check — on iOS/macOS a duplicate mid-handshake still runs the reset below.)
    //
    // This was a latched m_isConnecting bool until #1669. It read live-ish state
    // nowhere: it was cleared from the transport callbacks and the init timer,
    // none of which run when BLEManager's direct-connect abort severs the
    // controller's signals before deleting it — and startInitSequence() never
    // armed that timer, because characteristics never became ready. So the flag
    // stayed set for the life of the process and every later attempt was refused,
    // leaving the scale reachable only by forgetting and re-pairing it. The bug
    // was that it latched, not merely that it was redundant with the transport
    // debounce that landed two weeks after it. Decaid guards on live transport
    // state for the same reason (acaia_scale.dart onConnect).
    if (isConnected()) {
        ACAIA_LOG("Already connected, ignoring duplicate request");
        return;
    }
    if (m_transport && m_transport->isConnected()) {
        // Linked but no weight has ever arrived, so the handshake is wedged
        // rather than healthy. WARN, not LOG: a repeat here is the fingerprint of
        // a scale that connects and stays mute, and "Already connected" at debug
        // level reads as reassurance while the ladder spins.
        ACAIA_WARN("Connect requested while the link is up but no weight has arrived; "
                   "ignoring. Repeats here mean the handshake is wedged.");
        return;
    }

    // Stop any pending timers from previous connection
    stopAllTimers();

    // Reset state for new connection
    m_isPyxis = false;
    m_pyxisServiceFound = false;
    m_ipsServiceFound = false;
    m_characteristicsReady = false;
    m_receivingNotifications = false;
    m_weightReceived = false;
    m_identRetryCount = 0;
    m_infoFrameCount = 0;
    m_resyncLogged = false;
    m_badBatteryLogged = false;
    m_buffer.clear();

    m_name = device.name();
    m_transport->connectToDevice(device);
}

void AcaiaScale::onTransportConnected() {
    ACAIA_LOG(DECENZA_BLE_MSG_TRANSPORT_CONNECTED);
    m_transport->discoverServices();
}

void AcaiaScale::onTransportDisconnected() {
    ACAIA_INFO(DECENZA_BLE_MSG_TRANSPORT_DISCONNECTED);
    stopAllTimers();
    m_weightReceived = false;
    m_characteristicsReady = false;
    m_buffer.clear();   // the buffer outlives a notification now; don't carry a
                        // partial frame into the next session
    setConnected(false);
}

void AcaiaScale::onTransportError(const QString& message) {
    ACAIA_WARN(QString("Transport error: %1").arg(message));
    stopAllTimers();
    m_buffer.clear();

    // Drop the link, don't just mark ourselves disconnected. Both transports can
    // emit error() with the link still up and their own m_connected still true —
    // QtScaleBleTransport::onControllerError returns without touching it, and Qt
    // does not backfill a disconnected() for an error in the Discovered state.
    // Leaving that mismatch in place would strand connectToDevice()'s guard on a
    // transport that claims to be connected while this driver knows it is not:
    // the same permanent-refusal shape as #1669, just with a different flag.
    if (m_transport) {
        m_transport->disconnectFromDevice();
    }
    setConnected(false);
}

void AcaiaScale::onServiceDiscovered(const QBluetoothUuid& uuid) {
    ACAIA_LOG(QString("Service discovered: %1").arg(uuid.toString()));

    // Check for Pyxis service (newer Lunar 2021, Pyxis, etc.)
    if (uuid == Scale::Acaia::SERVICE) {
        ACAIA_LOG("Found Pyxis service");
        m_pyxisServiceFound = true;
    }
    // Check for IPS service (older Lunar, Pearl)
    else if (uuid == Scale::AcaiaIPS::SERVICE) {
        ACAIA_LOG("Found IPS service");
        m_ipsServiceFound = true;
    }
}

void AcaiaScale::onServicesDiscoveryFinished() {
    ACAIA_LOG("Service discovery finished");

    // Prefer Pyxis protocol if available (newer scales, including Lunar 2021)
    QBluetoothUuid serviceToUse;
    if (m_pyxisServiceFound) {
        m_isPyxis = true;
        serviceToUse = Scale::Acaia::SERVICE;
        ACAIA_LOG("Using Pyxis protocol");
    } else if (m_ipsServiceFound) {
        m_isPyxis = false;
        serviceToUse = Scale::AcaiaIPS::SERVICE;
        ACAIA_LOG("Using IPS protocol");
    } else {
        ACAIA_WARN("No compatible service found!");
        return;
    }

    m_transport->discoverCharacteristics(serviceToUse);
}

void AcaiaScale::onCharacteristicsDiscoveryFinished(const QBluetoothUuid& serviceUuid) {
    // Only handle our selected service
    if (m_isPyxis && serviceUuid != Scale::Acaia::SERVICE) return;
    if (!m_isPyxis && serviceUuid != Scale::AcaiaIPS::SERVICE) return;
    if (m_characteristicsReady) {
        ACAIA_LOG(DECENZA_BLE_MSG_DUPLICATE_CHARACTERISTICS);
        return;
    }

    ACAIA_LOG(QString("Characteristics discovered, protocol: %1").arg(m_isPyxis ? "Pyxis" : "IPS"));

    m_characteristicsReady = true;
    m_receivingNotifications = false;

    // Start the initialization sequence
    // Pyxis: notifications @ 500ms, then start init timer
    // IPS: notifications @ 100ms, then start init timer
    int notifyDelay = m_isPyxis ? 500 : 100;

    QTimer::singleShot(notifyDelay, this, &AcaiaScale::enableNotifications);
    QTimer::singleShot(notifyDelay + 500, this, &AcaiaScale::startInitSequence);
}

void AcaiaScale::enableNotifications() {
    if (!m_transport || !m_characteristicsReady) return;

    ACAIA_LOG("Enabling notifications");

    if (m_isPyxis) {
        m_transport->enableNotifications(Scale::Acaia::SERVICE, Scale::Acaia::STATUS);
    } else {
        m_transport->enableNotifications(Scale::AcaiaIPS::SERVICE, Scale::AcaiaIPS::CHARACTERISTIC);
    }
}

void AcaiaScale::startInitSequence() {
    if (!m_transport || !m_characteristicsReady) return;

    ACAIA_LOG("Starting init sequence");
    m_identRetryCount = 0;

    // Start recurring timer that sends ident + config
    m_initTimer->start(INIT_TIMER_INTERVAL_MS);

    // Send first ident immediately
    onInitTimer();
}

void AcaiaScale::onInitTimer() {
    // Check if we're receiving notifications (scale responded)
    if (m_receivingNotifications) {
        ACAIA_LOG("Scale responded, stopping init sequence and starting heartbeat");
        m_initTimer->stop();

        // Start heartbeat sequence
        sendConfig();
        QTimer::singleShot(1000, this, [this]() {
            if (m_transport && m_characteristicsReady) {
                sendHeartbeat();
            }
        });
        return;
    }

    // Check retry limit
    if (m_identRetryCount >= MAX_IDENT_RETRIES) {
        // Drop the link on the way out. The scale is linked but has never
        // answered, so leaving the transport connected strands us: this driver
        // never reaches setConnected(true) (that waits on a first weight), while
        // the transport still reports connected — and connectToDevice()'s guard
        // then refuses every tick of the 60s reconnect ladder forever. The old
        // m_isConnecting latch happened to clear itself here, so this exit has to
        // do it explicitly now that the guard reads transport state instead.
        //
        // infoFrames distinguishes the two failures worth telling apart: a scale
        // spamming msgType 7 is alive and rejecting our ident (wrong protocol or
        // write characteristic), whereas zero frames means notifications never
        // came up at all.
        ACAIA_WARN(QString("Init sequence failed after %1 retries "
                           "(infoFrames=%2, protocol=%3) — dropping the link so the "
                           "reconnect ladder can retry")
                       .arg(MAX_IDENT_RETRIES).arg(m_infoFrameCount)
                       .arg(m_isPyxis ? "Pyxis" : "IPS"));
        m_initTimer->stop();
        if (m_transport) {
            m_transport->disconnectFromDevice();
        }
        setConnected(false);
        return;
    }

    // Send ident and config
    sendIdent();
    // Config is sent after a short delay
    QTimer::singleShot(200, this, [this]() {
        if (m_transport && m_characteristicsReady && !m_receivingNotifications) {
            sendConfig();
        }
    });

    m_identRetryCount++;
    ACAIA_LOG(QString("Init attempt %1/%2").arg(m_identRetryCount).arg(MAX_IDENT_RETRIES));
}

void AcaiaScale::onCharacteristicChanged(const QBluetoothUuid& characteristicUuid, const QByteArray& value) {
    // Check if it's from our status characteristic
    if (m_isPyxis && characteristicUuid == Scale::Acaia::STATUS) {
        parseResponse(value);
    } else if (!m_isPyxis && characteristicUuid == Scale::AcaiaIPS::CHARACTERISTIC) {
        parseResponse(value);
    }
}

QByteArray AcaiaScale::encodePacket(uint8_t msgType, const QByteArray& payload) {
    QByteArray packet;
    packet.append(static_cast<char>(0xEF));  // Header 1
    packet.append(static_cast<char>(0xDD));  // Header 2
    packet.append(static_cast<char>(msgType));
    packet.append(payload);
    return packet;
}

void AcaiaScale::sendIdent() {
    ACAIA_LOG(QString("Sending ident, receivingNotifications: %1").arg(m_receivingNotifications ? "true" : "false"));

    // Ident message: type 0x0B with "01234567890123" + checksum
    QByteArray payload = QByteArray::fromHex("3031323334353637383930313233349A6D");
    QByteArray packet = encodePacket(0x0B, payload);
    sendCommand(packet);
    // Note: Timer scheduling is handled by onInitTimer(), not here
}

void AcaiaScale::sendConfig() {
    ACAIA_LOG("Sending config");

    // Config message: type 0x0C with notification settings
    QByteArray payload = QByteArray::fromHex("0900010102020103041106");
    QByteArray packet = encodePacket(0x0C, payload);
    sendCommand(packet);
}

void AcaiaScale::sendHeartbeat() {
    // Heartbeat message: type 0x00 with status bytes
    QByteArray payload = QByteArray::fromHex("02000200");
    QByteArray packet = encodePacket(0x00, payload);
    sendCommand(packet);

    // Only send config during init phase, before first weight received.
    // Once connected and receiving weight, config has done its job.
    if (!m_weightReceived) {
        QTimer::singleShot(1000, this, &AcaiaScale::sendConfig);
    }

    m_heartbeatTimer->start(3000);  // Heartbeat every 3 seconds
}

void AcaiaScale::sendCommand(const QByteArray& command) {
    if (!m_transport || !m_characteristicsReady) return;

    // Write to appropriate characteristic based on protocol
    // CRITICAL: IPS and Pyxis require different write types!
    // - IPS (older Lunar/Pearl): WriteWithoutResponse (fire and forget)
    // - Pyxis (newer Lunar 2021): WriteWithResponse (waits for ack)
    // This matches de1app's ble_write_type_no_response vs ble_write_type_default
    if (m_isPyxis) {
        m_transport->writeCharacteristic(Scale::Acaia::SERVICE, Scale::Acaia::CMD, command,
                                         ScaleBleTransport::WriteType::WithResponse);
    } else {
        // IPS uses same characteristic for read/write, and requires NO_RESPONSE write type
        m_transport->writeCharacteristic(Scale::AcaiaIPS::SERVICE, Scale::AcaiaIPS::CHARACTERISTIC, command,
                                         ScaleBleTransport::WriteType::WithoutResponse);
    }
}

void AcaiaScale::parseResponse(const QByteArray& data) {
    // Append to buffer
    m_buffer.append(data);

    // One notification can carry several concatenated frames — commonly a weight
    // frame with a 0x08 settings frame behind it. Decode every complete frame and
    // keep the trailing partial for the next notification.
    //
    // This used to decode the first frame and then clear the whole buffer, which
    // silently dropped whatever followed it. de1app scans forward past
    // uninteresting frames to reach a weight frame, but then clears its buffer
    // just the same (bluetooth.tcl acaia_scan_buffer_for_msg /
    // acaia_parse_response) — it picks a better frame, it does not carry the
    // rest. pyacaia (`bytes[messageEnd:]`), Beanconqueror (`bytes.slice(messageEnd)`)
    // and Decaid (`sublist(msgLen)`) all carry the remainder; this matches them.
    while (true) {
        const uint8_t* buf = reinterpret_cast<const uint8_t*>(m_buffer.constData());

        // Find message start (0xEF 0xDD)
        qsizetype msgStart = -1;
        for (qsizetype i = 0; i + 1 < m_buffer.size(); i++) {
            if (buf[i] == 0xEF && buf[i + 1] == 0xDD) {
                msgStart = i;
                break;
            }
        }

        if (msgStart < 0) {
            // No header in flight. A trailing 0xEF may be the first byte of a
            // header split across two notifications, so hold it back.
            if (!m_buffer.isEmpty() && static_cast<uint8_t>(m_buffer.back()) == 0xEF) {
                m_buffer = m_buffer.right(1);
            } else {
                m_buffer.clear();
            }
            return;
        }

        // Skip bytes before message start
        if (msgStart > 0) {
            m_buffer = m_buffer.mid(msgStart);
            buf = reinterpret_cast<const uint8_t*>(m_buffer.constData());
        }

        // Check if we have enough data for metadata
        if (m_buffer.size() < ACAIA_METADATA_LEN + 1) return;

        uint8_t msgType = buf[2];
        uint8_t length = buf[3];
        uint8_t eventType = buf[4];

        // Mark that we're receiving notifications (not just info messages)
        if (msgType != 7) {
            m_receivingNotifications = true;
        } else {
            // Counted, never logged per-frame: the scale spams these hardest
            // exactly when the handshake is failing, and the log ring drops on
            // overflow. The count is what the init-failure warning reports.
            m_infoFrameCount++;
        }

        // A bogus length would park the buffer forever waiting on bytes that
        // never arrive, now that the buffer survives across notifications.
        // 64 is de1app's ceiling too (bluetooth.tcl acaia_scan_buffer_for_msg,
        // whose own comment calls the threshold arbitrary), though it skips by
        // the untrusted length where we resync by 2 — resyncing on a length we
        // have just rejected is how one corrupt byte becomes a permanent desync.
        //
        // Worth a warning because this is the only desync-recovery path, and a
        // sustained run of it presents to the user as "connects, no weight".
        // One-shot: it sits inside the loop and could fire repeatedly per packet.
        if (length > MAX_ACAIA_PAYLOAD_LEN) {
            if (!m_resyncLogged) {
                m_resyncLogged = true;
                ACAIA_WARN(QString("Frame resync: length %1 exceeds the %2-byte ceiling "
                                   "(msgType=%3, buffered=%4)")
                               .arg(length).arg(MAX_ACAIA_PAYLOAD_LEN)
                               .arg(msgType).arg(m_buffer.size()));
            }
            m_buffer = m_buffer.mid(2);
            continue;
        }

        // Check if we have the complete message
        const qsizetype msgEnd = ACAIA_METADATA_LEN + length;
        if (m_buffer.size() < msgEnd) return;   // wait for the rest of the frame

        // Everything below indexes within THIS frame, so bound reads by msgEnd and
        // not by m_buffer.size(). The buffer now carries following frames, so a
        // frame whose length byte is too short for the body it claims would
        // otherwise read its neighbour's bytes and publish a garbage weight —
        // which feeds stop-at-weight.
        if (msgType == 0x0C && eventType == 5) {
            if (msgEnd >= ACAIA_METADATA_LEN + 6) {
                decodeWeight(m_buffer.left(msgEnd), ACAIA_METADATA_LEN);
            }
        } else if (msgType == 0x0C && eventType == 11) {
            // Heartbeat response. buf[7] (payload[2] counting from buf[5], the
            // base this file's weight path uses) selects the body: 5 = weight,
            // 7 = timer. de1app and Decaid decode the weight unconditionally;
            // pyacaia and Beanconqueror check the selector. A timer body decoded
            // as a weight is garbage, so follow the stricter pair.
            if (msgEnd > 7 && buf[7] == 5 && msgEnd >= ACAIA_METADATA_LEN + 3 + 6) {
                decodeWeight(m_buffer.left(msgEnd), ACAIA_METADATA_LEN + 3);
            }
        }

        // Settings response (msgType 0x08): contains battery level.
        //
        // Absolute offsets, because this block and the weight path above number
        // their payloads from different bases — the weight path from buf[5]
        // (ACAIA_METADATA_LEN), this one from buf[3] to match pyacaia's slice:
        //   buf[3] = the frame length, already consumed as `length` above
        //   buf[4] = battery
        //   buf[5] = units (2 = grams, 5 = ounces)
        //
        // This read buf[5] until issue #1670 — the units byte, so a scale set to
        // grams reported a permanent "2%" battery (ounces would have read 5%).
        // de1app parses no 0x08 frame at all, but three references land on buf[4]:
        // pyacaia (`Settings(bytes[messageStart+3:])`, `payload[1] & 0x7F`),
        // Beanconqueror (`parseSettings(bytes.slice(messageStart+3))`,
        // `payload[1] & 127`) and Decaid (`_commandBuffer[4]`, unmasked).
        if (msgType == 0x08) {
            // 0x7F yields 0-127; the <= 100 test below is what bounds it to a
            // percentage. Keep them separate — 101..127 means buf[4] is not a
            // battery byte, i.e. the offset above is wrong for this model, and
            // that is exactly the evidence #1670 lacked for months. One-shot so a
            // wrong offset cannot flood the log.
            const int batteryLevel = buf[4] & 0x7F;
            if (batteryLevel <= 100) {
                setBatteryLevel(batteryLevel);
            } else if (!m_badBatteryLogged) {
                m_badBatteryLogged = true;
                ACAIA_WARN(QString("Battery byte out of range: %1 (buf[3..5]=%2 %3 %4, "
                                   "protocol=%5) — payload offset likely wrong for this model")
                               .arg(batteryLevel).arg(buf[3]).arg(buf[4]).arg(buf[5])
                               .arg(m_isPyxis ? "Pyxis" : "IPS"));
            }
        }

        // Consume the frame and look for the next one in the same buffer
        m_buffer = m_buffer.mid(msgEnd);
    }
}

void AcaiaScale::decodeWeight(const QByteArray& data, int payloadOffset) {
    if (data.size() < payloadOffset + 6) return;

    const uint8_t* payload = reinterpret_cast<const uint8_t*>(data.constData()) + payloadOffset;

    // Weight is 3 bytes, little-endian
    int32_t value = ((payload[2] & 0xFF) << 16) |
                    ((payload[1] & 0xFF) << 8) |
                    (payload[0] & 0xFF);

    // Unit is in payload[4]
    uint8_t unit = payload[4] & 0xFF;
    double weight = value / std::pow(10.0, unit);

    // Sign is in payload[5]
    bool isNegative = payload[5] > 1;
    if (isNegative) {
        weight = -weight;
    }

    // Mark as connected only after receiving first valid weight
    // This ensures the handshake completed successfully
    if (!m_weightReceived) {
        m_weightReceived = true;
        ACAIA_INFO(DECENZA_BLE_MSG_CONNECTED("first weight frame"));
        setConnected(true);
    }

    setWeight(weight);
}

void AcaiaScale::sendKeepAlive() {
    // No keep-alive needed — the 3s heartbeat packets (sendHeartbeat()) keep the BLE
    // link alive. Periodic CCCD re-writes are unnecessary and risk AuthorizationError
    // disconnects.
}

void AcaiaScale::sendTareCommand() {
    // Tare message: type 0x04 with zeros
    QByteArray payload(17, 0);
    QByteArray packet = encodePacket(0x04, payload);
    sendCommand(packet);
}

void AcaiaScale::tare() {
    // Acaia Lunar scales are notoriously unreliable with single tare commands.
    // The Decent app sends 3-4 tares at shot start. We do the same here.
    ACAIA_LOG("Sending multiple tares (Acaia workaround)");

    // Send first tare immediately
    sendTareCommand();

    // Send 2 more tares with 100ms delays
    QTimer::singleShot(100, this, [this]() {
        if (m_transport && m_characteristicsReady) {
            sendTareCommand();
        }
    });
    QTimer::singleShot(200, this, [this]() {
        if (m_transport && m_characteristicsReady) {
            sendTareCommand();
        }
    });
}
