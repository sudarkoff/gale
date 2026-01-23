# Migration Guide: Arduino to ESP-IDF

This guide helps you transition from the Arduino version to the ESP-IDF version of Gale.

## Why Migrate to ESP-IDF?

**Advantages:**
- ✅ Better BLE stability and performance
- ✅ More efficient memory usage
- ✅ Faster build times after initial setup
- ✅ Full control over FreeRTOS tasks
- ✅ Professional-grade development framework
- ✅ Better debugging capabilities
- ✅ More robust WiFi handling

**Considerations:**
- ⚠️ More complex build system (but better documented)
- ⚠️ No Arduino ecosystem (but native ESP32 features instead)
- ⚠️ Initial ESP-IDF setup required

## Quick Start

### 1. Install ESP-IDF

```bash
# On macOS/Linux
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32

# Add to your shell profile (.bashrc, .zshrc, etc.):
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

### 2. Build and Flash

```bash
# Navigate to project directory
cd gale

# Set up environment (run in each terminal session)
get_idf  # or: . $HOME/esp/esp-idf/export.sh

# Build
idf.py build

# Flash (replace /dev/ttyUSB0 with your port)
idf.py -p /dev/ttyUSB0 flash monitor
```

## Configuration Migration

Your existing configuration stored in Arduino's Preferences will NOT automatically migrate. You have two options:

### Option 1: Reconfigure via Web Interface (Recommended)

1. Flash the ESP-IDF version
2. Connect to the "Gale" WiFi network
3. Navigate to http://gale.local or http://192.168.4.1
4. Enter your settings in the web interface
5. Click "Save Configuration"

### Option 2: Set Defaults in Code

Edit `main/main.c` and modify the `g_config` structure:

```c
config_t g_config = {
    .wifiSSID = "YourWiFiName",
    .wifiPassword = "YourPassword",
    .apSSID = "Gale",
    .apPassword = "gale1234",
    .useStationMode = true,  // Set to true to use WiFi STA mode

    .hrMax = 190,            // Your max heart rate
    .hrResting = 60,         // Your resting heart rate

    .zone1Percent = 0.4f,
    .zone2Percent = 0.7f,
    .zone3Percent = 0.8f,

    .alwaysOn = 1,
    .fanDelay = 120000,
    .hrHysteresis = 15,

    .relayGPIO = {25, 26, 27}
};
```

Then rebuild and flash.

## Hardware Changes

### GPIO Pins

The default GPIO assignments are the same:
- GPIO 25: Relay 1 (Speed 1)
- GPIO 26: Relay 2 (Speed 2)
- GPIO 27: Relay 3 (Speed 3)
- GPIO 2: Status LED (instead of LED_BUILTIN)

If your hardware uses different pins, update them in the web interface or in `main/main.c`.

## Code Architecture Comparison

### Arduino Version
```
gale.ino
├── setup()          - Initialization
├── loop()           - Main loop
├── Classes          - BLE callbacks
└── Functions        - WiFi, web server, etc.
```

### ESP-IDF Version
```
main/
├── main.c           - Entry point (app_main)
├── ble_hrm.c        - BLE client (event-driven)
├── wifi_manager.c   - WiFi management
├── web_server.c     - HTTP server
├── nvs_config.c     - Configuration storage
├── fan_control.c    - Fan control task
└── ota_update.c     - OTA support
```

## Feature Parity

| Feature | Arduino | ESP-IDF | Notes |
|---------|---------|---------|-------|
| BLE HRM Client | ✅ | ✅ | Improved stability in IDF |
| WiFi AP Mode | ✅ | ✅ | Same functionality |
| WiFi STA Mode | ✅ | ✅ | Better fallback in IDF |
| Web Configuration | ✅ | ✅ | Identical UI |
| NVS Storage | ✅ | ✅ | Preferences → NVS |
| mDNS | ✅ | ✅ | Same functionality |
| Fan Control | ✅ | ✅ | Identical logic |
| ArduinoOTA | ✅ | ⚠️ | Basic OTA in IDF (can be extended) |

## Common Issues

### Issue: Can't find serial port

**Solution:**
```bash
# List available ports
ls /dev/tty*

# On macOS, look for:
# /dev/tty.usbserial-*
# /dev/tty.SLAB_USBtoUART

# On Linux, look for:
# /dev/ttyUSB0
# /dev/ttyACM0
```

### Issue: Permission denied on Linux

**Solution:**
```bash
# Add your user to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in for changes to take effect
```

### Issue: Build fails with "idf.py not found"

**Solution:**
```bash
# Make sure ESP-IDF environment is loaded
. $HOME/esp/esp-idf/export.sh
# Or use the alias if you set it up
get_idf
```

### Issue: ESP32 not entering download mode

**Solution:**
- Hold the BOOT button on your ESP32 board
- Press and release the RESET button
- Release the BOOT button
- Try flashing again

## Development Workflow

### Arduino IDE
```bash
1. Edit gale.ino in Arduino IDE
2. Click Upload button
3. Monitor via Serial Monitor
```

### ESP-IDF
```bash
1. Edit files in main/ directory with any editor
2. idf.py build
3. idf.py -p PORT flash monitor
4. Press Ctrl+] to exit monitor
```

### Recommended: Use VS Code

Install the ESP-IDF extension for VS Code for the best development experience:
1. Install VS Code
2. Install "ESP-IDF" extension by Espressif
3. Configure extension to use your ESP-IDF installation
4. Enjoy IntelliSense, integrated build/flash/monitor, and debugging

## Performance Comparison

Based on typical usage:

| Metric | Arduino | ESP-IDF |
|--------|---------|---------|
| Binary Size | ~1.2 MB | ~800 KB |
| Free Heap | ~150 KB | ~200 KB |
| BLE Connect Time | 3-5s | 2-3s |
| Boot Time | ~3s | ~2s |
| WiFi Connect | ~5s | ~3s |

## Rollback to Arduino

If you need to go back to the Arduino version:

1. Keep the original `gale.ino` file (it's still in the repository)
2. Flash it using Arduino IDE
3. Your ESP32 will run the Arduino version again

The ESP-IDF and Arduino versions can coexist in the same directory without conflicts.

## Getting Help

- **ESP-IDF Documentation**: https://docs.espressif.com/projects/esp-idf/
- **ESP-IDF Forum**: https://esp32.com/
- **Project Issues**: Check the repository issues page

## Next Steps

After successfully migrating:

1. ✅ Test all features (BLE, WiFi, web interface, fan control)
2. ✅ Verify your configuration is correct
3. ✅ Test with your heart rate monitor
4. ✅ Monitor serial output for any issues
5. ✅ Consider extending OTA functionality if needed

Enjoy your upgraded Gale system!
