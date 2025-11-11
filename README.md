# FZ35 Lab ESP8266 Interface

This project adds a WiFi / Web UI / logging layer around an XY-FZ35 electronic DC load (or compatible firmware) using an ESP8266 (e.g. NodeMCU). It provides:
- Live measurements (V / A / Ah / time / computed W)
- Parameter visualization (OVP, OCP, OPP, LVP, OAH, OHP, recommended test load)
- Battery profile selection (predefined chemistries + protection limits)
- Graph of recent samples (timestamped)
- Automatic test logging (capacity + duration) persisted to LittleFS
- Time synchronization via browser or NTP

## Main Components

| File | Purpose |
|------|---------|
| FZ35_Lab.ino | Entry point, scheduling, parsing serial frames, test detection |
| FZ35_Comm.h | Serial command I/O, retries, success classification |
| FZ35_Battery.(h/cpp) | Battery profiles, selection, clamping, staged parameter application |
| FZ35_WebUI.h | Embedded HTML/JS dashboard + REST API endpoints |
| FZ35_TestLog.(h/cpp) | Persistent CSV test log + JSON serialization |
| FZ35_Graph.h | Simple ring buffer structure (legacy / optional) |
| FZ35_WiFi.h | WiFi provisioning & server startup |

## Hardware Summary

- ESP8266 (NodeMCU) pins used:
  - RX_PIN (15) / TX_PIN (13) for SoftwareSerial link to FZ35
- Ensure level compatibility and common ground.
- Power the ESP8266 separately if the load introduces noise.

## Build & Flash

1. Install Arduino IDE or PlatformIO.
2. Select Board: NodeMCU 1.0 (ESP-12E Module).
3. Set Flash Size with LittleFS enabled (e.g. 4M/1M).
4. Place project files in one directory (already provided).
5. Upload filesystem if using LittleFS (Arduino LittleFS plugin or `pio run -t uploadfs`).
6. Flash sketch.

## WiFi Behavior

- Attempts autoConnect with captive portal using SSID `FZ35-Lab`.
- If credentials fail: remains in AP mode for configuration.
- Once connected, the Web UI is served over port 80.

## Web UI Overview

Mounted at `/`:
- Parameter cards (protection + live measurements)
- Enable / Disable load controls
- Battery profile selector
- Graph canvas (auto-refresh every 2 s)
- Test results table (auto-refresh every 30 s)
- Time sync button

Removed Buttons:
- Manual Start / Stop measurement
- Manual Refresh (auto-poll replaces this)

## REST Endpoints

| Endpoint | Description |
|----------|-------------|
| `/params` | JSON of protection + live measurement fields |
| `/cmd?op=enable|disable|start|stop` | Control operations (start/stop kept for compatibility) |
| `/batteries` | List of battery profile names + active index |
| `/select_batt?idx=N` | Queue new profile |
| `/data?points=N` | Latest N samples: `[v,i,p,ts]` |
| `/test_results` | Logged discharge sessions |
| `/clear_test_log` | Erase log (FIFO memory + file) |
| `/get_time` | Current device epoch seconds |
| `/set_time?ts=<epoch>` | Set device time (browser sync) |

## Battery Profiles

Each profile defines:
- `nominalVoltage`
- `capacityAh`
- `maxLoadA`
- `recommendedLoadA`
- `lowVoltageProtect` (LVP)
- `overAhLimit` (OAH)
- `overHourLimit` (OHP, HH:MM)

Selection queues parameters; application sequence:
1. stop
2. load current (recommended)
3. OCP / OPP / LVP / OAH / OHP / OVP (with variants)
4. start

## Test Logging

Triggered automatically:
- Start: first time current > threshold (≈0.05 A)
- End: current falls below near-zero (<0.01 A)
- When valid capacity > minimal threshold, result saved to CSV `/testlog.csv`.

JSON format:
```
{
  "results":[
    {"date":"YYYY-MM-DD HH:MM","battery":"Name","capacity":Ah,"time":Hours},
    ...
  ]
}
```

## Time Sync

- NTP attempted on boot (pool.ntp.org + time.nist.gov).
- Fallback if unsynchronized (time < reasonable epoch).
- Browser button (`Sync Time`) calls `/set_time`.

## Extending

- Add new profiles in `batteryModules[]`.
- Adjust power/current limits (`RATED_*`) if using different hardware.
- Enhance graph scaling or add multi-series overlays.

## Known Limitations

- SoftwareSerial reliability depends on wiring & baud (9600 chosen).
- No authentication layer for endpoints (add if exposed publicly).
- Graph scaling of current/power uses naive normalization (optimize if needed).

## License

Add your chosen license (MIT / Apache-2.0 / GPL). Example MIT:

```
MIT License (add full text here)
```

## Contributing

1. Fork repository.
2. Create feature branch.
3. Add tests or demo captures if modifying parsing.
4. Submit PR with concise description.

## Troubleshooting

| Issue | Cause | Fix |
|-------|-------|-----|
| No test log saved | Current never dropped below threshold | Verify wiring / cutoff logic |
| Time remains 1970 | NTP blocked | Use manual sync button |
| Parameters not fully applied | Device response unpredictable | Increase timeouts / retry variants |
| Graph empty | No serial frames parsed | Check TX/RX crossing and baud |

## Quick Start

1. Flash firmware.
2. Connect to `FZ35-Lab` AP if portal opens.
3. Visit `http://<assigned-ip>/`.
4. Select battery profile.
5. Enable load.
6. Observe progression; review results table.

Enjoy reproducible battery discharge characterization over WiFi.
