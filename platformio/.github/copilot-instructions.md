# Copilot Instructions — esp32-weather-epd

Purpose: quick, actionable guidance for AI coding assistants working on this PlatformIO-based ESP32 e-paper weather display.

- **Big picture**: firmware runs on an ESP32, fetches OpenWeatherMap OneCall + air pollution APIs, reads an onboard BME sensor, renders a multi-panel e-paper display and then deep-sleeps. Main flow: `src/main.cpp` (setup) -> `client_utils.cpp` (network + time) -> `api_response.*` (JSON parsing) -> `renderer.cpp` / `display_utils.*` (draw pages) -> deep sleep logic in `main.cpp` (`beginDeepSleep`).

- **Key files to inspect**:
  - File: [platformio.ini](platformio.ini) — PlatformIO envs and board defaults (default_envs = `dfrobot_firebeetle2_esp32e`).
  - File: [src/main.cpp](src/main.cpp) — program entry, power/battery handling, NVS use, display refresh sequence and deep-sleep timing.
  - File: [src/client_utils.cpp](src/client_utils.cpp) — WiFi, NTP sync, HTTP(S) calls to OWM, and error-return conventions (see negative offsets: `-512` for WiFi status, `-256-<jsonErr>` for JSON errors).
  - File: [include/config.h](include/config.h) and [src/config.cpp](src/config.cpp) — compile-time feature flags (display type, driver, sensors, units) and runtime constants (pins, credentials, OWM_APIKEY).
  - File: [src/renderer.cpp](src/renderer.cpp) — GxEPD-based paged rendering, font and icon usage; see `display.firstPage()`/`display.nextPage()` loops.
  - Dir: `lib/esp32-weather-epd-assets/` — bundled fonts and icon bitmaps; change fonts by editing `FONT_HEADER` in `include/config.h`.

- **Build / flash / monitor**:
  - Build: `pio run` (uses `platformio.ini` default env). To target specific env: `pio run -e firebeetle32`.
  - Upload/Flash: `pio run -t upload` or `pio run -t upload -e dfrobot_firebeetle2_esp32e`.
  - Serial monitor: `pio device monitor -b 115200` (use `monitor_speed` in `platformio.ini`).
  - Note: cert management referenced in `include/config.h` — `USE_HTTPS_WITH_CERT_VERIF` requires regenerating `include/cert.h` (project mentions a `cert.py` helper). Keep API keys out of commits (`OWM_APIKEY` is in `src/config.cpp`).

- **Project-specific conventions & patterns**:
  - Heavy use of compile-time macros to select device features (display variants, drivers, sensor type, unit systems). Exactly-one checks are enforced in `include/config.h` — do not change those checks.
  - Display rendering uses paged drawing (GxEPD). Always call `initDisplay()` then a `do { ... } while (display.nextPage());` loop, and `powerOffDisplay()` when finished.
  - Networking: `startWiFi()` / `killWiFi()` pair; network failures are treated as recoverable and trigger a controlled UI message then deep sleep.
  - Error-return conventions: network functions return HTTP codes on success, and use reserved negative ranges for internal errors (`-512` series for WiFi, `-256` series for JSON deserialization). Preserve these when modifying callers.
  - Time: NTP sync must succeed before rendering; `waitForSNTPSync()` is used in `main.cpp`.
  - Low-power: `beginDeepSleep()` aligns wake times and uses macros in `include/config.h` and constants in `src/config.cpp` — be careful when altering sleep math.
  - Fonts & display layout: layout tuned for `FreeSans` by default. Changing `FONT_HEADER` may require visual tweaks in `renderer.cpp`.

- **Testing / debugging tips**:
  - Set `DEBUG_LEVEL` in `include/config.h` to increase serial verbosity; level 2 prints raw API responses.
  - To reproduce rendering locally, open serial log to confirm JSON parsing and then run incremental display draws on-device. The e-paper driver uses a paged API so UI unit tests are manual.

- **Integration points / external deps**:
  - PlatformIO packages: See `platformio.ini` (`GxEPD2`, `ArduinoJson`, Adafruit sensors). Use `pio lib update` to refresh libraries.
  - OpenWeatherMap: `client_utils.cpp` builds request URIs using `OWM_ENDPOINT`, `OWM_APIKEY`, `OWM_ONECALL_VERSION` (set in `src/config.cpp`). Keep API key lifecycle in mind when sharing patches.
  - Certificates: when `USE_HTTPS_WITH_CERT_VERIF` is enabled, `include/cert.h` must be maintained (project note: use `cert.py`).

If something in these notes is unclear or you want more examples (e.g., common refactors, how to change display resolution, or how to add a new locale), tell me which area to expand.
