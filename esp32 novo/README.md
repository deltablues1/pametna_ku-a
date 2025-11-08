# ESP32 Smart Home Controller v3.0

## Description
ESP32-based smart home controller with W5500 Ethernet, 16 lights, 14 outlets, 2 PIR sensors, 16 buttons via MCP23017 I²C expanders.

## Hardware requirements
- ESP32 DevKit
- 3x MCP23017 I²C expanders (0x20, 0x22, 0x27)
- W5500 Ethernet module (optional, SPI, 3.3V)
- PIR sensors, buttons, relays (as per io_map.h)

## Software requirements
- PlatformIO (VSCode extension or CLI)
- Python 3.x (for gzip script)

## Configuration
- Copy `include/secrets.h.example` to `include/secrets.h` and fill in Wi-Fi/MQTT credentials
- Edit `include/config.h` to adjust pins, timeouts, etc.
- To enable W5500 Ethernet: uncomment `-D W5500_ETHERNET` in `platformio.ini`

## Build and upload
- `pio run -t buildfs` - builds LittleFS filesystem image (gzips web UI)
- `pio run -t uploadfs` - uploads filesystem to ESP32
- `pio run -t upload` - compiles and uploads firmware
- `pio device monitor` - opens serial monitor (115200 baud)

## Network modes
- If W5500 enabled: tries Ethernet first, falls back to Wi-Fi STA, then AP
- If W5500 disabled: tries Wi-Fi STA, falls back to AP
- AP mode: SSID "ESP32-Setup", password "12345678", IP 192.168.4.1

## Web interface
- Access via IP address shown in serial monitor
- Home tab: control lights and outlets
- Events tab: view system events, filter, export

## Safe-restore behavior
- On boot: all outputs set LOW
- After 1 second: first saved output restored
- Every 1 second: next saved output restored
- Prevents power surge on startup

## MQTT (optional)
- Set `g_mqtt.enabled = true` in main.cpp
- Configure host/port in secrets.h
- Topics: `home/esp32/light/<i>/state`, `home/esp32/light/<i>/set`, etc.

## Troubleshooting
- If MCP not detected: check I²C wiring and addresses
- If W5500 fails: check SPI wiring and 3.3V power
- If Wi-Fi fails: check SSID/password in secrets.h
- To clear NVS: hold BOOT button during reset

## License
MIT or as specified

## Credits
Based on existing ESP32 smart home system, refactored for W5500 Ethernet and safe-restore
