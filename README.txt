fslogger ESP-IDF port

This project has been rewritten from the original Arduino implementation to ESP-IDF for `esp32-s3-devkitc-1` and is meant to be built with PlatformIO.

What it does
- Logs UART traffic to LittleFS.
- Starts a SoftAP web UI after 10 seconds of UART inactivity.
- Serves the log list, log contents, download/save actions, and basic charting from the on-device filesystem.

Defaults
- Board: `esp32-s3-devkitc-1`
- UART port: `UART1`
- RX pin: `GPIO18`
- TX pin: disabled (`-1`)
- Baud rate: `38400`
- AP SSID: `hcLogs`
- Web UI: `http://192.168.4.1/`

Build and flash
1. Create or reuse the local virtualenv if needed.
2. Run `.venv/bin/pio run`
3. Run `.venv/bin/pio run -t upload`
4. Run `.venv/bin/pio device monitor`

Project layout
- `src/main.cpp`: ESP-IDF firmware
- `src/CMakeLists.txt`: component registration and LittleFS image generation
- `spiffs_data/`: files flashed into the LittleFS partition
- `partitions.csv`: 8 MB ESP32-S3 partition table

Notes
- The original Arduino `data/` and library files are left in the repo as reference material, but the ESP-IDF build uses `spiffs_data/`.
- If your ESP32-S3 board uses different UART pins or flash size, update `platformio.ini` and `partitions.csv` accordingly.
