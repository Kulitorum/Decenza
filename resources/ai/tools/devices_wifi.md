# devices_wifi

WiFi scale discovery, without a BLE scan. A diagnostic pair: `action=browse` starts discovery,
`action=results` reads what it found. Wait a few seconds between them.

## browse

Runs DNS-SD for `_decentscale._tcp` plus the `hds` / `hds-2` / `hds-3` A-record fallback.

`backend` selects the mDNS implementation so the two can be compared on the same network:

| Value | Meaning |
|---|---|
| `auto` | What ships — bonjour on macOS/iOS, mjansson elsewhere |
| `mjansson` | On macOS, exercises the backend Android and Windows/Linux actually use |
| `bonjour` | Apple-only; unavailable elsewhere |

`timeoutMs` defaults to 8000. Short windows return the resolver's cache including stale entries;
longer ones let it prune them.

## results

Returns the DNS-SD detail the device list flattens away: `instanceName`, `mdnsName`, `hostname`,
`address`, `port`, `path`, `firmwareVersion`, and `foundBy` (service browse or hostname
fallback).

It also returns `browseRan` and `fallbackProbeRan`. **False means that transport could not run at
all** — no backend, socket refused, Local Network permission denied — which is a completely
different problem from running and finding nothing. A count of 0 cannot make that distinction.
