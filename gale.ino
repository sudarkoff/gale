/**
 * Control a 3-speed fan with heart rate.
 * Lots of code borrowed from https://github.com/agrabbs/hrm_fan_control
 * Modifications by George Sudarkoff <george@sudarkoff.com>
 * 
 * - Re-implement HR zones, do not turn on the fan until HR is above the first threshold
 *   (this is handy if you're using a Whoop strap, for example, and just happen to be in the
 *   vicinity of the fan, but not working out.
 * - Add hysteresis to delay lowering the speed when HR falls below the threshold, this
 *   somewhat debounces the HR readings and also compensates for the delay between the HR
 *   falling and how hot the body feels.
 * - Delay turning the fan off by a minute after HRM disconnects, to prevent the fan turning off
 *   because of temporary BLE connection hiccups.
 * - Light-up the built-in LED to indicate when the HRM is connected
 * - TODO: pulse the LED when the fan is on.
 * - FIX: "lld_pdu_get_tx_flush_nb HCI packet count mismatch (0, 1)" that happens somewhere
 *   after "Created client" and before "Connected to server"
*/

#include "BLEDevice.h"

#define _DEBUG

// Set to true to define Relay as Normally Open (NO)
#define RELAY_NO true

// Heart Rate Zones
#ifdef _DEBUG
#define ZONE_1 70
#define ZONE_2 80
#define ZONE_3 90
#else
#define ZONE_1 90
#define ZONE_2 135
#define ZONE_3 165
#endif

// Hysteresis (delay lowering the fan speed when the HR is falling)
// This both "debounces" the HR readings AND accounts for the lag between
// the HR rate and how hot your body feels.
#define HR_HYSTERESIS 10

// Number of relays to control the fan speed
#define NUM_RELAYS 3

// GPIO pins to control fan speed relays
uint8_t relayGPIO[NUM_RELAYS] = {25, 26, 27};

// The remote service we wish to connect to.
static BLEUUID serviceUUID("0000180d-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service we are interested in.
static BLEUUID charUUID(BLEUUID((uint16_t)0x2A37));

static boolean doConnect = false;
static boolean connected = false;
static double disconnectedTime = 0.0;
#ifdef _DEBUG
// Delay switching to a lower speed or turning the fan off
// to compensate for the lag between the heart rate and how how you feel.
// Also to prevent the fan from turning off abruptly in the middle of the workout
// if BT loses the connection for a moment.
static double fanDelay = 10000.0;     // 10 seconds
#else
static double fanDelay = 60000.0 * 2; // 2 minutes
#endif

static boolean notification = false;
static boolean doScan = false;
static uint8_t prevSpeed = 0;
static uint8_t currentSpeed = 0;
static BLEScan* pBLEScan;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

void printHRandFanSpeed(uint8_t hr, uint8_t zone, int8_t speed)
{
  Serial.print(" - HR: ");
  Serial.print(hr, DEC);
  Serial.print(" bpm");
  Serial.print(", ZONE ");
  Serial.print(zone, DEC);
  Serial.print(", FAN SPEED: ");
  Serial.println(speed, DEC);
}

static void calculateFanSpeed(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify)
{
  // HRM is possibly disconnected, we'll deal with that elsewhere
  if (pData[1] == 0) return;
  
  // ZONE 0 -> FAN OFF
  if (pData[1] < ZONE_1 - HR_HYSTERESIS)
  {
    currentSpeed = 0;
    printHRandFanSpeed(pData[1], 0, currentSpeed);
  }
  // ZONE 1
  else if ((currentSpeed < 1 && pData[1] >= ZONE_1 && pData[1] < ZONE_2) ||
           (currentSpeed > 1 && pData[1] < ZONE_2 - HR_HYSTERESIS)
          )
  {
    currentSpeed = 1;
    printHRandFanSpeed(pData[1], 1, currentSpeed);
  }
  // ZONE 2
  else if ((currentSpeed < 2 && pData[1] >= ZONE_2 && pData[1] < ZONE_3) ||
           (currentSpeed > 2 && pData[1] < ZONE_3 - HR_HYSTERESIS)
          )
  {
    currentSpeed = 2;
    printHRandFanSpeed(pData[1], 2, currentSpeed);
  }
  // ZONE 3
  else if (currentSpeed < 3 && pData[1] >= ZONE_3)
  {
    currentSpeed = 3;
    printHRandFanSpeed(pData[1], 3, currentSpeed);
  }
}

class HRMClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient)
    {
      // Turn the builtin LED on to indicate BT HRM is connected
      digitalWrite(LED_BUILTIN, HIGH);
      Serial.println(" - HRM connected");
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
      Serial.println(" - HRM disconnected, start the timer");
    }
};

bool connectToServer() {
  Serial.print(" - Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient*  pClient  = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new HRMClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr)
  {
    Serial.print(" - Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr)
  {
    Serial.print(" - Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  // Read the value of the characteristic.
  if (pRemoteCharacteristic->canRead())
  {
    std::string value = pRemoteCharacteristic->readValue();
    Serial.print(" - The characteristic value was: ");
    Serial.println(value.c_str());
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
      Serial.print("BLE Advertised Device found: ");
      Serial.println(advertisedDevice.toString().c_str());

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

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Gale...");
  BLEDevice::init("Gale");

  // Set all relays to off when the program starts - if set to Normally Open (NO), the relay is off when you set the relay to HIGH
  for (int i = 0; i < NUM_RELAYS; ++i)
  {
    pinMode(relayGPIO[i], OUTPUT);
    if (RELAY_NO)
    {
      digitalWrite(relayGPIO[i], HIGH);
    }
    else
    {
      digitalWrite(relayGPIO[i], LOW);
    }
  }

  // initialize digital pin LED_BUILTIN as an output for BT Connected status.
  pinMode(LED_BUILTIN, OUTPUT);

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new HRMAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  doScan = true;
}

void loop() {
  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true)
  {
    if (!connectToServer())
    {
      Serial.println("WARNING: failed to connect to the BLE server.");
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected && notification == false)
  {
    Serial.println(" - Turn notification on");
    const uint8_t onPacket[] = {0x01, 0x0};
    pRemoteCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)onPacket, 2, true);
    notification = true;
  }

  // The fan is on, but we're no longer connected to HRM
  if(!connected && currentSpeed > 0)
  {
    double currentTime = millis();
    if ((currentTime - disconnectedTime) > fanDelay)
    {
      // It's been long enough, giving up on HRM reconnecting and turning off the fan
      Serial.println(" - HRM failed to re-connect, bail and turn off the fan");
      currentSpeed = 0;
    }
  }

  if (!connected && doScan)
  {
    pBLEScan->start(1);
  }

  // Change the fan speed if needed
  if (currentSpeed != prevSpeed)
  {
    Serial.print(" - Set the fan speed to ");
    Serial.println(currentSpeed, DEC);
    for (int i = 0; i < NUM_RELAYS; ++i)
    {
      digitalWrite(relayGPIO[i], i == currentSpeed-1 ? LOW : HIGH);
    }
    prevSpeed = currentSpeed;
  }
}
