# Gale - ESP-IDF Version

Heart rate controlled fan using ESP32 and Bluetooth Low Energy heart rate monitor.

This is the ESP-IDF port of the original Arduino project. It provides better control over the system, improved performance, and more robust BLE handling.

## Features

- **BLE Heart Rate Monitor Client**: Automatically discovers and connects to BLE heart rate monitors
- **Multi-Speed Fan Control**: Controls a 3-speed fan based on heart rate zones
- **WiFi Configuration**: Can operate as Access Point or connect to existing WiFi network
- **Web Configuration Interface**: Modern web UI for configuring all settings
- **Persistent Storage**: Configuration saved to NVS flash
- **mDNS Support**: Access device at `gale.local`
- **OTA Updates**: Foundation for over-the-air firmware updates

## Requirements

### Hardware
- ESP32 development board (tested on ESP32-WROOM-32)
- 3-channel relay module (for controlling 3-speed fan)
- BLE heart rate monitor (with Heart Rate Service UUID 0x180D)

### Software
- ESP-IDF v5.0 or later
- Python 3.7+

## Installation

### 1. Install ESP-IDF

Follow the official ESP-IDF installation guide:
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

### 2. Clone and Build

```bash
# Navigate to the project directory
cd gale

# Set up ESP-IDF environment (run this in each new terminal session)
. $HOME/esp/esp-idf/export.sh

# Configure the project (optional - defaults should work)
idf.py menuconfig

# Build the project
idf.py build

# Flash to ESP32 (replace PORT with your serial port, e.g., /dev/ttyUSB0)
idf.py -p PORT flash

# Monitor serial output
idf.py -p PORT monitor
```

## Configuration

### Default Settings

**WiFi:**
- Access Point SSID: `Gale`
- Access Point Password: `gale1234`
- Station Mode: Disabled

**Heart Rate:**
- Maximum HR: 200 BPM
- Resting HR: 50 BPM
- Zone 1: 40% of HR reserve
- Zone 2: 70% of max HR
- Zone 3: 80% of max HR

**Fan Behavior:**
- Always On: Yes (fan runs at minimum speed below zone 1)
- Speed Change Delay: 120 seconds (2 minutes)
- HR Hysteresis: 15 BPM

**GPIO Pins:**
- Relay 1 (Speed 1): GPIO 25
- Relay 2 (Speed 2): GPIO 26
- Relay 3 (Speed 3): GPIO 27
- LED (BLE Status): GPIO 2

### Web Configuration Interface

1. Connect to the Gale WiFi network (or find it on your network if configured in Station mode)
2. Navigate to `http://gale.local` or `http://192.168.4.1` (AP mode)
3. Configure settings via the web interface
4. Click "Save Configuration" - device will restart with new settings

## Project Structure

```
gale/
├── CMakeLists.txt              # Main CMake file
├── sdkconfig.defaults          # Default SDK configuration
├── main/
│   ├── CMakeLists.txt          # Component CMake file
│   ├── main.c                  # Application entry point
│   ├── gale.h                  # Common header file
│   ├── ble_hrm.c              # BLE heart rate monitor client
│   ├── wifi_manager.c         # WiFi management (AP/STA modes)
│   ├── web_server.c           # HTTP web server and API
│   ├── nvs_config.c           # NVS configuration storage
│   ├── fan_control.c          # Fan speed control logic
│   └── ota_update.c           # OTA update support
└── README_IDF.md              # This file
```

## Key Differences from Arduino Version

### Improvements
1. **Native BLE Stack**: Uses ESP-IDF's native BLE GATT client instead of Arduino BLE library
2. **FreeRTOS Tasks**: Proper task-based architecture for concurrent operations
3. **Better Memory Management**: More efficient use of ESP32's memory
4. **Robust WiFi Handling**: Better WiFi event handling and fallback mechanisms
5. **No External Dependencies**: All code is self-contained (except cJSON for JSON parsing)

### Changes
1. **No ArduinoOTA**: The ESP-IDF version includes basic OTA support but not the full ArduinoOTA implementation. You can extend `ota_update.c` to add more features.
2. **Simplified JSON Parsing**: The web server uses manual JSON parsing for POST requests instead of ArduinoJson library.
3. **LED Pin**: Uses GPIO 2 (built-in LED on most ESP32 boards) instead of `LED_BUILTIN`.

## How It Works

### BLE Connection
1. On startup, the device scans for BLE devices advertising the Heart Rate Service (UUID 0x180D)
2. When found, it connects and subscribes to heart rate notifications
3. The built-in LED turns on when connected to a heart rate monitor
4. If disconnected, it automatically rescans and reconnects

### Fan Speed Control
The fan speed is controlled based on heart rate zones:

- **Speed 0** (Off): Below Zone 1 (only if `alwaysOn` is disabled)
- **Speed 1**: Zone 1 to Zone 2 (default: 40-70% of max HR)
- **Speed 2**: Zone 2 to Zone 3 (default: 70-80% of max HR)
- **Speed 3**: Above Zone 3 (default: >80% of max HR)

**Smart Speed Changes:**
- Speed increases immediately when heart rate rises
- Speed decreases are delayed by `fanDelay` (default: 2 minutes) to prevent rapid cycling
- Hysteresis prevents rapid speed changes near zone boundaries

### WiFi Modes

**Access Point Mode** (default):
- Creates a WiFi network named "Gale"
- Device IP: 192.168.4.1
- Use this for initial setup

**Station Mode**:
- Connects to your existing WiFi network
- Automatically falls back to AP mode if connection fails
- Configure via web interface

## Troubleshooting

### Build Errors

**Missing ESP-IDF:**
```bash
# Make sure ESP-IDF is properly installed and environment is set up
. $HOME/esp/esp-idf/export.sh
```

**Component errors:**
```bash
# Clean and rebuild
idf.py fullclean
idf.py build
```

### Runtime Issues

**Can't find device on network:**
- Check if device is in AP mode (look for "Gale" WiFi network)
- Try accessing directly via IP: http://192.168.4.1
- Check serial monitor for IP address assignment

**BLE not connecting:**
- Ensure heart rate monitor is powered on and in pairing mode
- Check serial monitor for BLE scan results
- Make sure heart rate monitor supports BLE Heart Rate Profile

**Fan not responding:**
- Verify GPIO pin connections match configuration
- Check relay module power supply
- Monitor serial output for fan speed changes

### Debugging

Enable verbose logging:
```bash
idf.py menuconfig
# Navigate to: Component config -> Log output -> Default log verbosity
# Select "Debug" or "Verbose"
```

View serial output:
```bash
idf.py -p PORT monitor
```

## Advanced Configuration

### Changing GPIO Pins

Edit `main/gale.h` or configure via web interface. Default pins:
- GPIO 25, 26, 27: Relay outputs
- GPIO 2: Status LED

### Custom Heart Rate Zones

Zones can be configured via the web interface or by modifying the defaults in `main/main.c`:

```c
.zone1Percent = 0.4f,  // 40% of HR reserve
.zone2Percent = 0.7f,  // 70% of max HR
.zone3Percent = 0.8f,  // 80% of max HR
```

### Partition Table

The default partition table supports OTA updates. To customize, create a `partitions.csv` file:

```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000
phy_init, data, phy,     0xf000,  0x1000
factory,  app,  factory, 0x10000, 1M
ota_0,    app,  ota_0,   ,        1M
ota_1,    app,  ota_1,   ,        1M
```

Then configure it:
```bash
idf.py menuconfig
# Navigate to: Partition Table -> Partition Table -> Custom partition table CSV
```

## License

Original code borrowed from https://github.com/agrabbs/hrm_fan_control

Modifications by George Sudarkoff <george@sudarkoff.com>

## Support

For issues and questions, please refer to the project repository.
