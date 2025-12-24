/**
 * Control a 3-speed fan with a heart rate monitor.
 * Some code borrowed from https://github.com/agrabbs/hrm_fan_control
 * 
 * Modifications by George Sudarkoff <george@sudarkoff.com>:
 * 
 * - Re-implement how the HR to Speed is calculated (implement hysteresis and timed delay)
 * - Light-up the built-in LED to indicate when the HRM is connected
*/

#include "BLEDevice.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

//#define _DEBUG
#define RELAY_NO
#define NUM_RELAYS 3

// RELAY_NO -> Normally Open Relay
#ifdef RELAY_NO
#define RELAY_ON LOW
#define RELAY_OFF HIGH
#else
#define RELAY_ON HIGH
#define RELAY_OFF LOW
#endif

// Configuration structure
struct Config {
  // WiFi settings
  char wifiSSID[32];
  char wifiPassword[64];
  char apSSID[32];
  char apPassword[64];
  bool useStationMode;

  // Heart rate settings
  uint8_t hrMax;
  uint8_t hrResting;

  // Zone thresholds (as percentages)
  float zone1Percent;  // Percent of HR reserve
  float zone2Percent;  // Percent of max HR
  float zone3Percent;  // Percent of max HR

  // Fan behavior settings
  uint8_t alwaysOn;         // 0=turn off below zone1, 1=keep on
  uint32_t fanDelay;        // Delay before lowering speed (ms)
  uint8_t hrHysteresis;     // BPM hysteresis for debouncing

  // GPIO pins
  uint8_t relayGPIO[NUM_RELAYS];
};

// Default configuration
Config config = {
  // WiFi defaults
  "",           // wifiSSID (empty = not configured)
  "",           // wifiPassword
  "Gale",       // apSSID
  "gale1234",   // apPassword (minimum 8 chars for WPA2)
  false,        // useStationMode

  // Heart rate defaults (original values)
  200,          // hrMax
  50,           // hrResting

  // Zone defaults
  0.4,          // zone1Percent (40% of HR reserve)
  0.7,          // zone2Percent (70% of max HR)
  0.8,          // zone3Percent (80% of max HR)

  // Fan behavior defaults
  1,            // alwaysOn
  #ifdef _DEBUG
  10000,        // fanDelay (10 seconds in debug)
  0,            // hrHysteresis (none in debug)
  #else
  120000,       // fanDelay (2 minutes)
  15,           // hrHysteresis
  #endif

  // GPIO defaults
  {25, 26, 27}  // relayGPIO
};

// Calculated zone thresholds (computed from config)
float zone1, zone2, zone3;

// WiFi and Web Server objects
WebServer server(80);
Preferences preferences;

// The remote service we wish to connect to.
static BLEUUID serviceUUID("0000180d-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service we are interested in.
static BLEUUID charUUID(BLEUUID((uint16_t)0x2A37));

static boolean doConnect = false;
static boolean connected = false;
static double disconnectedTime = 0.0;
static double speedChangedTime = 0.0;

static boolean notification = false;
static boolean doScan = false;
static uint8_t prevSpeed = 0;
static uint8_t currentSpeed = 1;  // Will be set from config.alwaysOn
static BLEScan* pBLEScan;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

static void calculateFanSpeed(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify)
{
  // HRM is possibly disconnected, we'll deal with that elsewhere
  if (pData[1] == 0) return;

  // ZONE 0 -> FAN OFF (or minimum speed if alwaysOn)
  if (currentSpeed > 0 && pData[1] < zone1)
  {
    currentSpeed = config.alwaysOn;
    speedChangedTime = millis();
  }
  // ZONE 1
  else if ((currentSpeed < 1 && pData[1] >= zone1 && pData[1] < zone2) ||
           (currentSpeed > 1 && pData[1] < zone2 - config.hrHysteresis)
          )
  {
    currentSpeed = 1;
    speedChangedTime = millis();
  }
  // ZONE 2
  else if ((currentSpeed < 2 && pData[1] >= zone2 && pData[1] < zone3) ||
           (currentSpeed > 2 && pData[1] < zone3 - config.hrHysteresis)
          )
  {
    currentSpeed = 2;
    speedChangedTime = millis();
  }
  // ZONE 3
  else if (currentSpeed < 3 && pData[1] >= zone3)
  {
    currentSpeed = 3;
    speedChangedTime = millis();
  }
}

class HRMClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient)
    {
      // Turn the builtin LED on to indicate BT HRM is connected
      digitalWrite(LED_BUILTIN, HIGH);
    }

    void onDisconnect(BLEClient* pclient)
    {
      // Turn off the builtin LED to indicate BT HRM is disconnected
      digitalWrite(LED_BUILTIN, LOW);

      // Record the time when the HRM disconnected, then
      // stop the fan in the loop() if the HRM doesn't
      // re-connect within fanDelay milliseconds
      disconnectedTime = millis();
      connected = false;
    }
};

bool connectToServer() {
  BLEClient*  pClient  = BLEDevice::createClient();

  pClient->setClientCallbacks(new HRMClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr)
  {
    Serial.print(F("Failed to find our service UUID: "));
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr)
  {
    Serial.print(F("Failed to find our characteristic UUID: "));
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }

  // Read the value of the characteristic.
  if (pRemoteCharacteristic->canRead())
  {
    std::string value = pRemoteCharacteristic->readValue();
  }

  if (pRemoteCharacteristic->canNotify())
  {
    pRemoteCharacteristic->registerForNotify(calculateFanSpeed);
  }

  connected = true;
  return true;
}

/**
   Scan for BLE servers and find the first one that advertises the service we are looking for.
*/
class HRMAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    /**
        Called for each advertising BLE server.
    */
    void onResult(BLEAdvertisedDevice advertisedDevice)
    {
      // We have found a device, let us now see if it contains the service we are looking for.
      if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID))
      {
        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
        doScan = true;
      }
    }
};

void setFanSpeed(uint8_t fanSpeed)
{
  if (fanSpeed == prevSpeed) return;

  double currentTime = millis();
  // If the speed is going up—change it right away
  // or wait fanDelay ms before lowering it
  if ((fanSpeed > prevSpeed) || (currentTime - speedChangedTime) > config.fanDelay)
  {
    for (int i = 0; i < NUM_RELAYS; ++i)
    {
      digitalWrite(config.relayGPIO[i], i == fanSpeed-1 ? RELAY_ON : RELAY_OFF);
    }
    prevSpeed = fanSpeed;
  }
}

// Calculate zone thresholds from config
void calculateZones() {
  float hrReserve = config.hrMax - config.hrResting;
  zone1 = config.hrResting + (config.zone1Percent * hrReserve);
  zone2 = config.zone2Percent * config.hrMax;
  zone3 = config.zone3Percent * config.hrMax;
}

// Load configuration from Preferences
void loadConfig() {
  preferences.begin("gale", true); // Read-only mode

  // WiFi settings
  preferences.getString("wifiSSID", config.wifiSSID, sizeof(config.wifiSSID));
  preferences.getString("wifiPass", config.wifiPassword, sizeof(config.wifiPassword));
  preferences.getString("apSSID", config.apSSID, sizeof(config.apSSID));
  preferences.getString("apPass", config.apPassword, sizeof(config.apPassword));
  config.useStationMode = preferences.getBool("useStation", false);

  // Heart rate settings
  config.hrMax = preferences.getUChar("hrMax", 190);
  config.hrResting = preferences.getUChar("hrRest", 40);

  // Zone percentages
  config.zone1Percent = preferences.getFloat("zone1Pct", 0.4);
  config.zone2Percent = preferences.getFloat("zone2Pct", 0.7);
  config.zone3Percent = preferences.getFloat("zone3Pct", 0.8);

  // Fan behavior
  config.alwaysOn = preferences.getUChar("alwaysOn", 1);
  config.fanDelay = preferences.getUInt("fanDelay", 120000);
  config.hrHysteresis = preferences.getUChar("hrHyst", 15);

  // GPIO pins
  preferences.getBytes("gpios", config.relayGPIO, sizeof(config.relayGPIO));

  preferences.end();

  // Calculate zones from loaded config
  calculateZones();

  Serial.println(F("Configuration loaded"));
  Serial.printf("HR Max: %d, Resting: %d\n", config.hrMax, config.hrResting);
  Serial.printf("Zone 1: %.1f, Zone 2: %.1f, Zone 3: %.1f\n", zone1, zone2, zone3);
}

// Save configuration to Preferences
void saveConfig() {
  preferences.begin("gale", false); // Read-write mode

  // WiFi settings
  preferences.putString("wifiSSID", config.wifiSSID);
  preferences.putString("wifiPass", config.wifiPassword);
  preferences.putString("apSSID", config.apSSID);
  preferences.putString("apPass", config.apPassword);
  preferences.putBool("useStation", config.useStationMode);

  // Heart rate settings
  preferences.putUChar("hrMax", config.hrMax);
  preferences.putUChar("hrRest", config.hrResting);

  // Zone percentages
  preferences.putFloat("zone1Pct", config.zone1Percent);
  preferences.putFloat("zone2Pct", config.zone2Percent);
  preferences.putFloat("zone3Pct", config.zone3Percent);

  // Fan behavior
  preferences.putUChar("alwaysOn", config.alwaysOn);
  preferences.putUInt("fanDelay", config.fanDelay);
  preferences.putUChar("hrHyst", config.hrHysteresis);

  // GPIO pins
  preferences.putBytes("gpios", config.relayGPIO, sizeof(config.relayGPIO));

  preferences.end();

  // Recalculate zones
  calculateZones();

  Serial.println(F("Configuration saved"));
}

// Setup WiFi in Station mode (connect to existing network)
bool setupWiFiStation() {
  if (strlen(config.wifiSSID) == 0) {
    Serial.println(F("No WiFi SSID configured"));
    return false;
  }

  Serial.printf("Connecting to WiFi: %s\n", config.wifiSSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSSID, config.wifiPassword);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print(F("Connected! IP address: "));
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println();
    Serial.println(F("Failed to connect to WiFi"));
    return false;
  }
}

// Setup WiFi in Access Point mode
void setupWiFiAP() {
  Serial.printf("Starting Access Point: %s\n", config.apSSID);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(config.apSSID, config.apPassword);

  IPAddress IP = WiFi.softAPIP();
  Serial.print(F("AP IP address: "));
  Serial.println(IP);
}

// Setup WiFi (tries Station mode first, falls back to AP)
void setupWiFi() {
  if (config.useStationMode && strlen(config.wifiSSID) > 0) {
    if (!setupWiFiStation()) {
      Serial.println(F("Falling back to Access Point mode"));
      setupWiFiAP();
    }
  } else {
    setupWiFiAP();
  }

  // Setup mDNS
  if (MDNS.begin("gale")) {
    Serial.println(F("mDNS responder started: gale.local"));
  }
}

// Web server route handlers
void setupWebServer() {
  // Serve the main configuration page
  server.on("/", HTTP_GET, []() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Gale Configuration</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background: #f5f5f7;
      padding: 20px;
      line-height: 1.6;
    }
    .container {
      max-width: 800px;
      margin: 0 auto;
      background: white;
      border-radius: 12px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
      padding: 30px;
    }
    h1 {
      color: #1d1d1f;
      margin-bottom: 10px;
      font-size: 32px;
    }
    .subtitle {
      color: #86868b;
      margin-bottom: 30px;
    }
    .section {
      margin-bottom: 30px;
      padding-bottom: 30px;
      border-bottom: 1px solid #d2d2d7;
    }
    .section:last-child {
      border-bottom: none;
    }
    h2 {
      color: #1d1d1f;
      margin-bottom: 15px;
      font-size: 22px;
    }
    .form-group {
      margin-bottom: 20px;
    }
    label {
      display: block;
      margin-bottom: 5px;
      color: #1d1d1f;
      font-weight: 500;
    }
    .help-text {
      font-size: 13px;
      color: #86868b;
      margin-top: 4px;
    }
    input[type="text"],
    input[type="password"],
    input[type="number"] {
      width: 100%;
      padding: 12px;
      border: 1px solid #d2d2d7;
      border-radius: 8px;
      font-size: 16px;
      transition: border-color 0.2s;
    }
    input:focus {
      outline: none;
      border-color: #0071e3;
    }
    .checkbox-group {
      display: flex;
      align-items: center;
      gap: 10px;
    }
    input[type="checkbox"] {
      width: 20px;
      height: 20px;
      cursor: pointer;
    }
    button {
      background: #0071e3;
      color: white;
      border: none;
      padding: 12px 24px;
      border-radius: 8px;
      font-size: 16px;
      font-weight: 500;
      cursor: pointer;
      transition: background 0.2s;
    }
    button:hover {
      background: #0077ed;
    }
    button:active {
      background: #006edb;
    }
    .status {
      margin-top: 20px;
      padding: 12px;
      border-radius: 8px;
      display: none;
    }
    .status.success {
      background: #d1f4e0;
      color: #03543f;
      display: block;
    }
    .status.error {
      background: #fde8e8;
      color: #9b1c1c;
      display: block;
    }
    .row {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 15px;
    }
    @media (max-width: 600px) {
      .row {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Gale</h1>
    <p class="subtitle">Heart Rate Controlled Fan Configuration</p>

    <form id="configForm">
      <div class="section">
        <h2>WiFi Settings</h2>
        <div class="form-group">
          <label for="apSSID">Access Point Name</label>
          <input type="text" id="apSSID" name="apSSID" required>
          <div class="help-text">Name of the WiFi network Gale creates</div>
        </div>
        <div class="form-group">
          <label for="apPassword">Access Point Password</label>
          <input type="password" id="apPassword" name="apPassword" minlength="8" required>
          <div class="help-text">Password must be at least 8 characters</div>
        </div>
        <div class="form-group">
          <div class="checkbox-group">
            <input type="checkbox" id="useStationMode" name="useStationMode">
            <label for="useStationMode" style="margin-bottom: 0;">Connect to existing WiFi network</label>
          </div>
        </div>
        <div id="stationFields" style="display: none;">
          <div class="form-group">
            <label for="wifiSSID">WiFi Network Name</label>
            <input type="text" id="wifiSSID" name="wifiSSID">
          </div>
          <div class="form-group">
            <label for="wifiPassword">WiFi Password</label>
            <input type="password" id="wifiPassword" name="wifiPassword">
          </div>
        </div>
      </div>

      <div class="section">
        <h2>Heart Rate Zones</h2>
        <div class="row">
          <div class="form-group">
            <label for="hrMax">Maximum Heart Rate (BPM)</label>
            <input type="number" id="hrMax" name="hrMax" min="100" max="250" required>
          </div>
          <div class="form-group">
            <label for="hrResting">Resting Heart Rate (BPM)</label>
            <input type="number" id="hrResting" name="hrResting" min="30" max="100" required>
          </div>
        </div>
        <div class="help-text">Zone 1: <span id="zone1Display">-</span> BPM | Zone 2: <span id="zone2Display">-</span> BPM | Zone 3: <span id="zone3Display">-</span> BPM</div>
      </div>

      <div class="section">
        <h2>Fan Behavior</h2>
        <div class="form-group">
          <div class="checkbox-group">
            <input type="checkbox" id="alwaysOn" name="alwaysOn">
            <label for="alwaysOn" style="margin-bottom: 0;">Keep fan on when heart rate is below Zone 1</label>
          </div>
        </div>
        <div class="row">
          <div class="form-group">
            <label for="fanDelay">Speed Change Delay (seconds)</label>
            <input type="number" id="fanDelay" name="fanDelay" min="0" max="600" required>
            <div class="help-text">Delay before reducing fan speed</div>
          </div>
          <div class="form-group">
            <label for="hrHysteresis">Hysteresis (BPM)</label>
            <input type="number" id="hrHysteresis" name="hrHysteresis" min="0" max="30" required>
            <div class="help-text">Prevents rapid speed changes</div>
          </div>
        </div>
      </div>

      <button type="submit">Save Configuration</button>
      <div id="status" class="status"></div>
    </form>
  </div>

  <script>
    // Load current configuration
    fetch('/api/config')
      .then(r => r.json())
      .then(data => {
        document.getElementById('apSSID').value = data.apSSID;
        document.getElementById('apPassword').value = data.apPassword;
        document.getElementById('useStationMode').checked = data.useStationMode;
        document.getElementById('wifiSSID').value = data.wifiSSID;
        document.getElementById('wifiPassword').value = data.wifiPassword;
        document.getElementById('hrMax').value = data.hrMax;
        document.getElementById('hrResting').value = data.hrResting;
        document.getElementById('alwaysOn').checked = data.alwaysOn == 1;
        document.getElementById('fanDelay').value = data.fanDelay / 1000;
        document.getElementById('hrHysteresis').value = data.hrHysteresis;
        updateZoneDisplay();
        toggleStationFields();
      });

    // Toggle station mode fields
    document.getElementById('useStationMode').addEventListener('change', toggleStationFields);

    function toggleStationFields() {
      const stationFields = document.getElementById('stationFields');
      stationFields.style.display = document.getElementById('useStationMode').checked ? 'block' : 'none';
    }

    // Update zone display when HR values change
    document.getElementById('hrMax').addEventListener('input', updateZoneDisplay);
    document.getElementById('hrResting').addEventListener('input', updateZoneDisplay);

    function updateZoneDisplay() {
      const hrMax = parseInt(document.getElementById('hrMax').value) || 0;
      const hrRest = parseInt(document.getElementById('hrResting').value) || 0;
      const reserve = hrMax - hrRest;
      const zone1 = Math.round(hrRest + (0.4 * reserve));
      const zone2 = Math.round(0.7 * hrMax);
      const zone3 = Math.round(0.8 * hrMax);
      document.getElementById('zone1Display').textContent = zone1;
      document.getElementById('zone2Display').textContent = zone2;
      document.getElementById('zone3Display').textContent = zone3;
    }

    // Handle form submission
    document.getElementById('configForm').addEventListener('submit', async (e) => {
      e.preventDefault();
      const formData = new FormData(e.target);
      const data = {
        apSSID: formData.get('apSSID'),
        apPassword: formData.get('apPassword'),
        useStationMode: formData.get('useStationMode') ? 1 : 0,
        wifiSSID: formData.get('wifiSSID') || '',
        wifiPassword: formData.get('wifiPassword') || '',
        hrMax: parseInt(formData.get('hrMax')),
        hrResting: parseInt(formData.get('hrResting')),
        alwaysOn: formData.get('alwaysOn') ? 1 : 0,
        fanDelay: parseInt(formData.get('fanDelay')) * 1000,
        hrHysteresis: parseInt(formData.get('hrHysteresis'))
      };

      try {
        const response = await fetch('/api/config', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(data)
        });

        const status = document.getElementById('status');
        if (response.ok) {
          status.className = 'status success';
          status.textContent = 'Configuration saved! Device will restart in 3 seconds...';
        } else {
          status.className = 'status error';
          status.textContent = 'Failed to save configuration';
        }
      } catch (err) {
        const status = document.getElementById('status');
        status.className = 'status error';
        status.textContent = 'Error: ' + err.message;
      }
    });
  </script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", html);
  });

  // API: Get current configuration
  server.on("/api/config", HTTP_GET, []() {
    String json = "{";
    json += "\"apSSID\":\"" + String(config.apSSID) + "\",";
    json += "\"apPassword\":\"" + String(config.apPassword) + "\",";
    json += "\"useStationMode\":" + String(config.useStationMode ? "true" : "false") + ",";
    json += "\"wifiSSID\":\"" + String(config.wifiSSID) + "\",";
    json += "\"wifiPassword\":\"" + String(config.wifiPassword) + "\",";
    json += "\"hrMax\":" + String(config.hrMax) + ",";
    json += "\"hrResting\":" + String(config.hrResting) + ",";
    json += "\"alwaysOn\":" + String(config.alwaysOn) + ",";
    json += "\"fanDelay\":" + String(config.fanDelay) + ",";
    json += "\"hrHysteresis\":" + String(config.hrHysteresis);
    json += "}";
    server.send(200, "application/json", json);
  });

  // API: Save configuration
  server.on("/api/config", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      String body = server.arg("plain");

      // Parse JSON manually (simple approach for limited fields)
      // In production, consider using ArduinoJson library

      // Extract values - this is a simplified parser
      int idx;

      // apSSID
      idx = body.indexOf("\"apSSID\":\"");
      if (idx != -1) {
        int start = idx + 10;
        int end = body.indexOf("\"", start);
        body.substring(start, end).toCharArray(config.apSSID, sizeof(config.apSSID));
      }

      // apPassword
      idx = body.indexOf("\"apPassword\":\"");
      if (idx != -1) {
        int start = idx + 14;
        int end = body.indexOf("\"", start);
        body.substring(start, end).toCharArray(config.apPassword, sizeof(config.apPassword));
      }

      // useStationMode
      idx = body.indexOf("\"useStationMode\":");
      if (idx != -1) {
        config.useStationMode = body.substring(idx + 17, idx + 18).toInt();
      }

      // wifiSSID
      idx = body.indexOf("\"wifiSSID\":\"");
      if (idx != -1) {
        int start = idx + 12;
        int end = body.indexOf("\"", start);
        body.substring(start, end).toCharArray(config.wifiSSID, sizeof(config.wifiSSID));
      }

      // wifiPassword
      idx = body.indexOf("\"wifiPassword\":\"");
      if (idx != -1) {
        int start = idx + 16;
        int end = body.indexOf("\"", start);
        body.substring(start, end).toCharArray(config.wifiPassword, sizeof(config.wifiPassword));
      }

      // hrMax
      idx = body.indexOf("\"hrMax\":");
      if (idx != -1) {
        config.hrMax = body.substring(idx + 8).toInt();
      }

      // hrResting
      idx = body.indexOf("\"hrResting\":");
      if (idx != -1) {
        config.hrResting = body.substring(idx + 12).toInt();
      }

      // alwaysOn
      idx = body.indexOf("\"alwaysOn\":");
      if (idx != -1) {
        config.alwaysOn = body.substring(idx + 11).toInt();
      }

      // fanDelay
      idx = body.indexOf("\"fanDelay\":");
      if (idx != -1) {
        config.fanDelay = body.substring(idx + 11).toInt();
      }

      // hrHysteresis
      idx = body.indexOf("\"hrHysteresis\":");
      if (idx != -1) {
        config.hrHysteresis = body.substring(idx + 15).toInt();
      }

      // Save to preferences
      saveConfig();

      server.send(200, "application/json", "{\"status\":\"ok\"}");

      // Restart after a short delay to apply WiFi changes
      delay(3000);
      ESP.restart();
    } else {
      server.send(400, "application/json", "{\"error\":\"No data\"}");
    }
  });

  server.begin();
  Serial.println(F("Web server started"));
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("Starting Gale..."));

  // Load configuration from Preferences
  loadConfig();

  // Setup WiFi
  setupWiFi();

  // Initialize BLE
  BLEDevice::init("Gale");

  // Set all relays to off when the program starts - if set to Normally Open (NO), the relay is off when you set the relay to HIGH
  for (int i = 0; i < NUM_RELAYS; ++i)
  {
    pinMode(config.relayGPIO[i], OUTPUT);
    digitalWrite(config.relayGPIO[i], RELAY_OFF);
  }

  // initialize digital pin LED_BUILTIN as an output for BT Connected status.
  pinMode(LED_BUILTIN, OUTPUT);

  // Setup OTA updates
  ArduinoOTA.setHostname("gale");
  ArduinoOTA.setPassword(config.apPassword);

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("OTA Update started: " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update complete");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println(F("OTA updates enabled"));

  // Setup web server routes (will be defined below)
  setupWebServer();

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new HRMAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  doScan = true;

  currentSpeed = config.alwaysOn;
}

void loop() {
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true)
  {
    if (!connectToServer())
    {
      Serial.println(F("Failed to connect to the BLE server."));
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected && notification == false)
  {
    const uint8_t onPacket[] = {0x01, 0x0};
    pRemoteCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)onPacket, 2, true);
    notification = true;
  }

  // Reconnect
  if (!connected && doScan)
  {
    pBLEScan->start(1);
  }

  // The fan is on, but we're no longer connected to HRM
  if(!connected && currentSpeed > 0)
  {
    double currentTime = millis();
    if ((currentTime - disconnectedTime) > config.fanDelay)
    {
      // It's been long enough, giving up on HRM reconnecting and turning off the fan
      Serial.println(F("Failed to re-connect to HRM"));
      currentSpeed = config.alwaysOn;
    }
  }

  setFanSpeed(currentSpeed);

  // Handle OTA updates
  ArduinoOTA.handle();

  // Handle web server requests
  server.handleClient();
}
