# Home Automation Integration

Decenza supports home automation integration via MQTT and REST API. This allows you to:

- Monitor machine state and telemetry in your home automation dashboard
- Wake/sleep the machine remotely (e.g., turn on when your alarm goes off)
- Integrate with Home Assistant, Node-RED, and other platforms

**Security Note:** Remote control is limited to wake/sleep only. Physical operations (espresso, steam, hot water, flush) cannot be triggered remotely - you must be at the machine with a portafilter/cup.

---

## Quick Start

### Option 1: REST API (simplest)

1. Enable **Remote Access** in Settings → Shot History tab
2. Note the URL shown (e.g., `http://192.168.1.100:8888`)
3. Access endpoints:
   - `GET /api/state` - Machine state
   - `GET /api/telemetry` - All sensor data
   - `POST /api/command` - Send wake/sleep commands

### Option 2: MQTT (for home automation)

1. Go to Settings → Home Automation tab
2. Enable MQTT and configure your broker (or tap "Scan" to find it)
3. If using Home Assistant, enable "Home Assistant Discovery" for automatic setup
4. The machine will appear as a device with sensors and a power switch

---

## REST API Reference

The REST API is served by the same HTTP server used for shot history (port 8888 by default). Enable it in Settings → Shot History → Remote Access.

### GET /api/state

Returns current machine state.

**Response:**
```json
{
  "connected": true,
  "state": "Idle",
  "substate": "ready",
  "phase": "Ready",
  "isFlowing": false,
  "isHeating": false,
  "isReady": true
}
```

**Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `connected` | boolean | Whether app is connected to DE1 via BLE |
| `state` | string | DE1 state: Sleep, Idle, Espresso, Steam, HotWater, Flush, etc. |
| `substate` | string | DE1 substate: ready, heating, preinfusion, pouring, ending, etc. |
| `phase` | string | App phase: Disconnected, Sleep, Idle, Heating, Ready, Preinfusion, Pouring, etc. |
| `isFlowing` | boolean | True during espresso/steam/hot water/flush operations |
| `isHeating` | boolean | True while machine is heating up |
| `isReady` | boolean | True when machine is ready to brew |

### GET /api/telemetry

Returns all current sensor readings.

**Response:**
```json
{
  "connected": true,
  "pressure": 0.0,
  "flow": 0.0,
  "temperature": 92.5,
  "mixTemperature": 91.8,
  "steamTemperature": 155.0,
  "waterLevel": 75,
  "waterLevelMl": 1200,
  "scaleWeight": 0.0,
  "scaleFlowRate": 0.0,
  "shotTime": 0.0,
  "targetWeight": 36.0,
  "state": "Idle",
  "substate": "ready",
  "phase": "Ready",
  "firmwareVersion": "1.2.3",
  "timestamp": "2024-01-19T10:30:00Z"
}
```

**Fields:**
| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `connected` | boolean | - | BLE connection status |
| `pressure` | number | bar | Current pressure at puck |
| `flow` | number | ml/s | Current flow rate |
| `temperature` | number | °C | Group head temperature |
| `mixTemperature` | number | °C | Mix valve temperature |
| `steamTemperature` | number | °C | Steam boiler temperature |
| `waterLevel` | number | % | Tank water level (0-100) |
| `waterLevelMl` | number | ml | Tank water level in milliliters |
| `scaleWeight` | number | g | Current scale weight |
| `scaleFlowRate` | number | g/s | Weight-based flow rate |
| `shotTime` | number | s | Elapsed shot time (during extraction) |
| `targetWeight` | number | g | Stop-at-weight target |
| `state` | string | - | DE1 state |
| `substate` | string | - | DE1 substate |
| `phase` | string | - | App phase |
| `firmwareVersion` | string | - | DE1 firmware version |
| `timestamp` | string | ISO 8601 | Server timestamp |

### POST /api/command

Execute a command. Only wake/sleep commands are supported.

**Request:**
```json
{
  "command": "wake"
}
```

**Valid commands:**
| Command | Description |
|---------|-------------|
| `wake` | Wake machine from sleep → idle |
| `sleep` | Put machine to sleep |

**Response (success):**
```json
{
  "success": true,
  "command": "wake"
}
```

**Response (error):**
```json
{
  "error": "Invalid command. Valid commands: wake, sleep"
}
```

### Existing Endpoints

The server also provides these existing endpoints:

| Endpoint | Description |
|----------|-------------|
| `GET /api/power/status` | Power state (legacy, use /api/state) |
| `GET /api/power/wake` | Wake machine (legacy) |
| `GET /api/power/sleep` | Sleep machine (legacy) |
| `GET /api/shots` | List all shots |
| `GET /api/shot/{id}` | Get shot details |
| `GET /` | Web interface for shot history |

---

## MQTT Reference

### Connection Settings

| Setting | Default | Description |
|---------|---------|-------------|
| Broker Host | (empty) | MQTT broker hostname or IP. Use "Scan" to discover via mDNS. |
| Port | 1883 | MQTT broker port (1883 standard, 8883 for TLS) |
| Username | (empty) | Optional authentication username |
| Password | (empty) | Optional authentication password |
| Client ID | auto | Unique client identifier (auto-generated if empty) |
| Base Topic | decenza | Root topic for all messages |

### Published Topics

All topics are prefixed with the base topic (default: `decenza/`).

| Topic | Payload | Update Trigger |
|-------|---------|----------------|
| `state` | "Sleep", "Idle", "Espresso", "Steam", "HotWater", "Flush" | On state change |
| `substate` | "ready", "heating", "preinfusion", "pouring", "ending" | On substate change |
| `phase` | Full phase name from app | On phase change |
| `connected` | "true" / "false" | On BLE connection change |
| `availability` | "online" / "offline" | On app connect/disconnect (LWT) |
| `temperature/head` | number (°C) | At publish interval |
| `temperature/mix` | number (°C) | At publish interval |
| `temperature/steam` | number (°C) | At publish interval |
| `pressure` | number (bar) | At publish interval |
| `flow` | number (ml/s) | At publish interval |
| `weight` | number (g) | At publish interval |
| `water_level` | number (%) | On change |
| `water_level_ml` | number (ml) | On change |
| `shot_time` | number (s) | During shot |
| `target_weight` | number (g) | On change |

### Command Topic

Subscribe to `{base_topic}/command` to receive commands. The app subscribes to this topic and responds to:

| Payload | Action |
|---------|--------|
| `wake` | Wake machine from sleep |
| `sleep` | Put machine to sleep |

**Example (mosquitto):**
```bash
# Wake the machine
mosquitto_pub -h broker_ip -t "decenza/command" -m "wake"

# Put to sleep
mosquitto_pub -h broker_ip -t "decenza/command" -m "sleep"
```

### Publishing Options

| Option | Default | Description |
|--------|---------|-------------|
| Publish Interval | 1000 ms | How often to publish telemetry (0 = only on change) |
| Retain Messages | true | Broker retains last value for new subscribers |
| Home Assistant Discovery | true | Publish discovery config for automatic HA setup |

---

## Home Assistant Integration

When "Home Assistant Discovery" is enabled, the app publishes MQTT discovery messages that Home Assistant automatically picks up.

### Automatically Created Entities

**Sensors:**
- `sensor.de1_state` - Machine state (Sleep, Idle, Espresso, etc.)
- `sensor.de1_temperature` - Head temperature (°C)
- `sensor.de1_pressure` - Pressure (bar)
- `sensor.de1_flow` - Flow rate (ml/s)
- `sensor.de1_weight` - Scale weight (g)
- `sensor.de1_water_level` - Water tank level (%)
- `sensor.de1_shot_time` - Shot elapsed time (s)

**Controls:**
- `switch.de1_power` - Wake/sleep toggle

### Device Info

All entities are grouped under a single device:
- **Name:** DE1 Espresso Machine
- **Manufacturer:** Decent Espresso
- **Model:** DE1

### Example Automations

**Wake machine at 6:30 AM on weekdays:**
```yaml
automation:
  - alias: "Wake espresso machine on weekday mornings"
    trigger:
      - platform: time
        at: "06:30:00"
    condition:
      - condition: time
        weekday:
          - mon
          - tue
          - wed
          - thu
          - fri
    action:
      - service: switch.turn_on
        target:
          entity_id: switch.de1_power
```

**Send notification when water is low:**
```yaml
automation:
  - alias: "Notify when DE1 water low"
    trigger:
      - platform: numeric_state
        entity_id: sensor.de1_water_level
        below: 20
    action:
      - service: notify.mobile_app
        data:
          title: "DE1 Water Low"
          message: "Water tank is at {{ states('sensor.de1_water_level') }}%"
```

**Turn off if idle for 2 hours:**
```yaml
automation:
  - alias: "Auto-sleep DE1 after 2 hours idle"
    trigger:
      - platform: state
        entity_id: sensor.de1_state
        to: "Idle"
        for:
          hours: 2
    action:
      - service: switch.turn_off
        target:
          entity_id: switch.de1_power
```

---

## mDNS Discovery

The app can scan your local network for MQTT brokers using mDNS (Bonjour/Avahi). This helps you find your broker without manually entering the IP address.

### How It Works

1. Tap "Scan" in the MQTT settings
2. The app broadcasts a query for `_mqtt._tcp` services
3. Any broker advertising via mDNS will appear in the list
4. Tap a result to fill in the host and port

### Enabling mDNS on Your Broker

**Mosquitto (with Avahi on Linux):**
```bash
# Install avahi-daemon if not present
sudo apt install avahi-daemon

# Create service file
sudo nano /etc/avahi/services/mqtt.service
```

```xml
<?xml version="1.0" standalone='no'?>
<!DOCTYPE service-group SYSTEM "avahi-service.dtd">
<service-group>
  <name>MQTT Broker</name>
  <service>
    <type>_mqtt._tcp</type>
    <port>1883</port>
  </service>
</service-group>
```

```bash
sudo systemctl restart avahi-daemon
```

**Home Assistant:** The built-in MQTT broker (if using the Mosquitto add-on) typically advertises via mDNS automatically.

---

## Troubleshooting

### MQTT Connection Issues

**"Connection refused"**
- Check broker host and port are correct
- Verify broker is running: `mosquitto -v`
- Check firewall allows port 1883

**"Bad username or password"**
- Verify credentials in app settings
- Check broker ACL configuration

**"Client ID rejected"**
- Try leaving Client ID empty (auto-generates unique ID)
- Another client may be using the same ID

### REST API Issues

**"Connection refused"**
- Enable Remote Access in Settings → Shot History
- Check the URL shown matches what you're using
- Verify firewall allows port 8888

**No data in responses**
- Ensure app is connected to DE1 via Bluetooth
- Check Bluetooth tab shows "Connected"

### Home Assistant Not Discovering

1. Verify MQTT is connected (check status in app)
2. Check Home Assistant MQTT integration is configured
3. Verify broker is the same one HA uses
4. Try "Publish Discovery Now" button
5. Check HA logs for MQTT errors

---

## Security Considerations

- **Local network only:** Both REST and MQTT are intended for local network use
- **No authentication on REST:** The HTTP API has no built-in authentication
- **Limited commands:** Only wake/sleep can be triggered remotely
- **Physical operations blocked:** Espresso, steam, hot water, and flush require being at the machine
- **Password storage:** MQTT password is stored in app settings (same as other passwords)

For remote access outside your home network, use a VPN rather than exposing these services directly to the internet.
