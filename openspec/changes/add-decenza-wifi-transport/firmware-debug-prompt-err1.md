# Firmware-Side Debug Prompt: BLE-Provisioned "Atlas" Fails (err=1) but Hardcoded "Atlas" Worked

This is the prompt to hand to the AI working on `c:\code\decenzascale` (the
ESP32-NimBLE firmware). The Decenza-side AI has finished implementing
the provisioning client and confirmed it on the wire; we now have a
specific reproducible failure that needs firmware-side investigation.

---

You're working on the DecenzaScale firmware (ESP32 / NimBLE) at
`c:\code\decenzascale`. The companion Decenza tablet app at
`c:\code\decenza` has finished its side of BLE-driven Wi-Fi provisioning,
but on first end-to-end test the provisioning fails in a way that
strongly suggests a firmware-side bug.

Your job is to find which firmware code path is mishandling the
NVS-stored credentials versus the hardcoded ones, and either fix it or
report back with a precise hypothesis and the supporting evidence.

## Background — what the user did

1. Built and flashed firmware with **hardcoded** SSID="Atlas" + password
   in NVS or `#define`. Confirmed working: scale joined "Atlas",
   Decenza connected over Wi-Fi, weight streamed correctly.
2. Removed the hardcoded constants from firmware code and reflashed.
3. Tapped "Forget Wi-Fi" in Decenza to clear local pairing.
4. Opened the Decenza wizard and provisioned the **same** SSID="Atlas"
   with the **same** password.
5. Provisioning failed with err=1 on fee4 STATUS.

Crucial point: **the AP and credentials are identical** between the
working and failing cases. Only the credential-source path
(`#define` vs BLE-written NVS) is different. The radio sees the AP
either way; the password is the same string either way.

## Failure signature (from Decenza's logcat)

```
[wifi/provisioning] provisionWifi() ssid=Atlas
[wifi/provisioning] BLE connected; discovering services
[wifi/provisioning] Provisioning service discovered
[wifi/provisioning] Connect command sent; awaiting STATUS
[wifi/provisioning] STATUS state=1 ip=- err=0       ← Connecting…
... (16 consecutive STATUS=1 notifications, ~1 per second) ...
[wifi/provisioning] STATUS state=3 ip=- err=1       ← Failed after ~16 s
[wifi/provisioning] Provisioning failed: Wi-Fi connection failed (err=1)
```

The 16-second timeout pattern is *not* characteristic of
"AP not found" — that fails faster on most ESP-IDF builds. It IS
characteristic of multiple WPA2 4-way-handshake retries failing in
sequence (each ~3 s × 4–5 retries = ~15 s before the supplicant gives
up). Combined with the fact that hardcoded credentials work for the
same AP, this is most likely a **passphrase corruption or truncation**
issue on the firmware-side credential ingestion path.

## Wire-protocol contract (from Decenza-side, locked-in)

Decenza writes three characteristics in sequence and serializes via
`characteristicWritten` callbacks:

```
fee1 (write,  ≤32 B): UTF-8 SSID — Decenza calls QString::toUtf8(),
                       which produces NO trailing NUL byte.
fee2 (write,  ≤64 B): UTF-8 passphrase — same, NO trailing NUL.
fee3 (write,    1 B): 0x01 to start STA association.
```

After fee3 write, Decenza waits for fee4 STATUS notifications. fee4
shape: `[state, rssi_int8, ip0..ip3, err]`.

Decenza-side has unit tests pinning the parser; the BLE writes are
serialized and complete-ack'd. If a write got dropped on the wire,
Decenza would log a Qt write error before sending the next one — that
log line is absent in the failure trace, so all three writes reached
firmware.

## Hypotheses, in priority order

### Hypothesis 1 — fee2 character buffer max-length is too small (most likely)

NimBLE characteristic definitions specify a `BLE_GATT_CHR_F_*` flag set
and a `max_len` attribute. The default is often 20 bytes (the BLE
classic minimum payload), which is fine for 6-character passwords but
truncates anything longer.

**Check**: locate the GATT table or characteristic registration for
`0000fee2-decc-1000-8000-00805f9b34fb`. Look for `max_len` or
equivalent buffer-size constant. If it's <64, raise it to 64.

**Verify**: the access callback for fee2 — does it copy
`ctxt->om` (the mbuf chain) bytes correctly, or does it `memcpy` a
fixed-size buffer? If fixed, what's the buffer size?

### Hypothesis 2 — passphrase isn't NUL-terminated when written into `wifi_config.sta.password`

`wifi_config_t.sta.password` is a 64-byte char array. ESP-IDF's
WPA supplicant treats it as a NUL-terminated C-string for length
calculation purposes. Decenza writes raw UTF-8 without a NUL.

If the firmware's code is:
```c
memcpy(wifi_config.sta.password, fee2_buffer, fee2_received_len);
// missing: wifi_config.sta.password[fee2_received_len] = '\0';
```
…and `wifi_config.sta.password` was zero-initialized, this works
*sometimes* (residual zero bytes from a clean buffer act as a NUL).
But if the buffer is reused across calls — or if the previous
hardcoded value left bytes in there — the password effectively becomes
"<actual_password><leftover_bytes>" which fails authentication.

**Check**: how is `wifi_config.sta.password` populated from the fee2
buffer? Specifically:
- Is the buffer explicitly zeroed (`memset(&wifi_config, 0, sizeof(wifi_config))`) before each population?
- Is a NUL terminator explicitly written after the memcpy?
- Is the source buffer (the BLE write target) reused or zeroed between writes?

### Hypothesis 3 — the hardcoded path differs from the NVS path in a setting other than the credentials

Firmware might have two code paths:
- **Hardcoded path**: `wifi_config.sta.ssid = WIFI_SSID; wifi_config.sta.password = WIFI_PASSWORD; esp_wifi_set_config(...)`. Other fields zero-initialized.
- **NVS path**: load credentials from NVS, populate `wifi_config`. Other fields might be set to non-default values.

Specifically check:
- `wifi_config.sta.threshold.authmode` — defaults to OPEN. If the NVS
  path sets it to e.g. `WIFI_AUTH_WPA3_PSK` and the AP only does
  WPA2-PSK, association fails silently.
- `wifi_config.sta.pmf_cfg.required` — if true and the AP doesn't
  support PMF, association fails.
- `wifi_config.sta.scan_method` — `WIFI_FAST_SCAN` won't find an AP
  outside the country code's allowed channels; `WIFI_ALL_CHANNEL_SCAN`
  is more reliable.
- `wifi_config.sta.bssid_set` — if set true with stale bytes, ESP-IDF
  will only connect to that specific BSSID.

**Check**: diff the `wifi_config_t` populated by the hardcoded path
against the one populated by the NVS path. They should be byte-identical
except for ssid/password contents.

### Hypothesis 4 — NVS storage encoding mismatch

If firmware stores fee1/fee2 to NVS and re-reads on association attempt,
the read path might use `nvs_get_str` (which expects NUL-terminated
strings) while the write path uses `nvs_set_blob` (which doesn't add
NUL). The blob is shorter than what `get_str` expects → it returns the
blob plus garbage from neighboring NVS entries, or fails with
`ESP_ERR_NVS_INVALID_LENGTH`.

**Check**: are fee1/fee2 stored in NVS? If yes, what's the get/set API
pair? Are they consistent?

### Hypothesis 5 — fee2 access-callback length parameter is wrong

NimBLE's GATT access callback signature is:
```c
int access_cb(uint16_t conn_handle, uint16_t attr_handle,
              struct ble_gatt_access_ctxt *ctxt, void *arg);
```

The actual write length is at `OS_MBUF_PKTLEN(ctxt->om)`. Some firmware
patterns mistakenly use `ctxt->om->om_len` which is only the FIRST
mbuf's length — long writes that span multiple mbufs get truncated to
just the first chunk's bytes.

**Check**: how does the fee2 access callback determine length? If it
uses `om_len` instead of `OS_MBUF_PKTLEN`, or uses `ble_hs_mbuf_to_flat`
with a too-small destination buffer, that's the bug.

## Diagnostic asks

To prove or disprove the above hypotheses, please return:

### 1. UART log of one full provisioning attempt

Set `CONFIG_LOG_DEFAULT_LEVEL_VERBOSE=y` (or call
`esp_log_level_set("wifi", ESP_LOG_VERBOSE)`) and capture the UART
output during one wizard run. Look for and report:

- The bytes received on fee1 (length and hex).
- The bytes received on fee2 (length and hex — yes, including the password; this is a debug session, the user can rotate the password after).
- The values populated into `wifi_config.sta.ssid` and
  `wifi_config.sta.password` immediately before
  `esp_wifi_set_config`. Print as both the raw bytes and as `printf("%s")`.
- The `wifi_config.sta.threshold.authmode`, `pmf_cfg.required`,
  `bssid_set`, and `scan_method` values.
- Any `WIFI_EVENT_STA_DISCONNECTED` events with their `reason` codes.

### 2. The same UART log when running with the hardcoded path

Reflash with the hardcoded SSID/password temporarily restored, capture
the same set of values. Then diff the two `wifi_config_t` structures.
The diff should be zero in every field except ssid/password contents.

### 3. The fee2 character-registration code

The exact NimBLE `ble_gatt_chr_def` (or equivalent) entry for fee2,
including any `max_len`, flags, and the access callback name. Plus the
access callback function body.

## What NOT to do

- Don't change the BLE-side wire protocol. The 4 characteristics
  (fee1/fee2/fee3/fee4) and their UUIDs are locked. The Decenza side
  has tests pinning the layout; flipping bytes around would force a
  Decenza rebuild.
- Don't try to make Decenza send a NUL-terminated string. Qt's
  `toUtf8()` doesn't produce one and reaching into Qt to force one is
  awkward; the firmware should defensively NUL-terminate on receipt.
- Don't add ack response packets to fee3 or fee4 just to debug — the
  Decenza side waits for STATUS notifications, not write-ack semantics.

## Reply format

When you have an answer, please reply with:

1. **Root cause**: which hypothesis (1–5) was correct, OR a new one
   you discovered. Include the smoking-gun line from the UART log.
2. **Fix landed**: file + function modified, brief description.
3. **Verification**: UART log excerpt from the post-fix run showing
   `wifi_config` populated correctly and `WIFI_EVENT_STA_GOT_IP`
   firing, OR an explanation of what test you ran.

Then the Decenza-side AI can resume manual smoke testing of the full
provisioning loop and move on to Phase 5 (calibration).
