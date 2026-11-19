/*
 * ArduinoRemote - ESP32-C3 SuperMini Bluetooth remote control
 *
 * Two buttons: one to go FORWARD, one to go BACK.
 *   - Pressing a physical button sends a command over Bluetooth Low Energy
 *     (BLE) to a connected phone / tablet / computer.
 *   - The same commands can be sent TO the board over BLE (write
 *     characteristic) so a host can drive it remotely.
 *
 * Pins (edit the CONFIG section below to match your board):
 *   BUTTON_FORWARD_PIN  GPIO 8  -> forward button (other leg to GND)
 *   BUTTON_BACK_PIN     GPIO 9  -> back button    (other leg to GND)
 *   LED_PIN             GPIO 10 -> status LED     (optional, set 255 to skip)
 *
 * Flash with the Arduino IDE or arduino-cli:
 *   Board: "MakerGO ESP32 C3 SuperMini"  (esp32:esp32:makergo_c3_supermini)
 *   or any ESP32-C3 board. The ESP32-C3 BLE radio is used; no external module.
 *
 * Pair / connect from:
 *   - The docs/controller.html Web Bluetooth page (works on Chrome/Edge over HTTPS)
 *   - nRF Connect (iOS/Android)
 *   - Any BLE central that writes to the RX characteristic
 */

#include <Arduino.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <BLECharacteristic.h>
#include <BLE2902.h>
#include <BLEAdvertising.h>

// ESP-IDF GAP function to set the BLE device name at runtime
extern "C" uint8_t esp_ble_gap_set_device_name(const char *name);

// ===========================================================================
// CONFIG
// ===========================================================================
static const char* DEFAULT_DEVICE_NAME = "ArduinoRemote";

// Preferences (NVS) storage for the device name
static const char* PREF_NAMESPACE = "arduinoremote";
static const char* PREF_KEY_NAME  = "name";
static const size_t MAX_DEVICE_NAME_LEN = 31;  // BLE device name max

// Buttons (active LOW, wire to GND, internal pull-up)
static const uint8_t BUTTON_FORWARD_PIN = 8;
static const uint8_t BUTTON_BACK_PIN    = 9;

// Status LED (set 255 to disable)
static const uint8_t LED_PIN = 10;

// BLE service / characteristic UUIDs (128-bit)
static const char* SERVICE_UUID          = "7f4f1234-1234-1234-1234-123456789abc";
static const char* CHARACTERISTIC_RX_UUID   = "7f4f1235-1234-1234-1234-123456789abc"; // write commands
static const char* CHARACTERISTIC_TX_UUID   = "7f4f1236-1234-1234-1234-123456789abc"; // notify responses
static const char* CHARACTERISTIC_RENAME_UUID = "7f4f1237-1234-1234-1234-123456789abc"; // write new name

// Commands sent over BLE
static const char CMD_FORWARD  = 'F';
static const char CMD_BACK     = 'B';
static const char CMD_STOP     = 'S';
static const char CMD_RENAME_OK    = 'R'; // rename succeeded
static const char CMD_RENAME_FAIL  = 'r'; // rename failed

static const uint32_t DEBOUNCE_MS = 50;

// Forward declarations
static void handleCommand(char cmd);
static void restartWithNewName();
static bool isValidDeviceName(const char* name, size_t length);
static void handlePairingResponse(uint32_t pin, bool accept);

// ===========================================================================
// BLE globals
// ===========================================================================
static BLEServer* pServer = nullptr;
static BLEService* pService = nullptr;
static BLECharacteristic* pRxChar = nullptr;
static BLECharacteristic* pTxChar = nullptr;
static BLECharacteristic* pRenameChar = nullptr;

static bool deviceConnected = false;
static bool oldDeviceConnected = false;
static bool pendingRestart = false;
static uint32_t renameRestartAt = 0;

// Device name (loaded from Preferences, mutable)
static char deviceName[32] = {0};

// Preferences (NVS)
static Preferences preferences;

// ===========================================================================
// Button state
// ===========================================================================
struct Button {
  uint8_t pin;
  char cmd;
  const char* name;
  uint32_t lastDebounceTime;
  int lastState;
  int currentState;
};

Button btnForward = {BUTTON_FORWARD_PIN, CMD_FORWARD, "FORWARD", 0, HIGH, HIGH};
Button btnBack    = {BUTTON_BACK_PIN,    CMD_BACK,    "BACK",    0, HIGH, HIGH};

// ===========================================================================
// BLE server callbacks
// ===========================================================================
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    (void)s;
    deviceConnected = true;
    if (LED_PIN != 255) digitalWrite(LED_PIN, HIGH);
    Serial.println("[BLE] Client connected");
  }
  void onDisconnect(BLEServer* s) override {
    (void)s;
    deviceConnected = false;
    if (LED_PIN != 255) digitalWrite(LED_PIN, LOW);
    Serial.println("[BLE] Client disconnected");
  }
};

// ===========================================================================
// BLE security / pairing callbacks (accept "Just Works" pairing)
// ===========================================================================
class SecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override {
    Serial.println("[BLE] Passkey request - using 000000");
    return 0;
  }
  void onPassKeyNotify(uint32_t pass_key) override {
    (void)pass_key;
    Serial.println("[BLE] Passkey notified");
  }
  bool onSecurityRequest() override {
    Serial.println("[BLE] Security request - accepting pairing");
    return true;
  }
  bool onConfirmPIN(uint32_t pin) override {
    (void)pin;
    Serial.println("[BLE] Pairing confirm requested (Just Works)");
    return true;
  }
  bool onAuthorizationRequest(uint16_t connHandle, uint16_t attrHandle, bool isRead) override {
    (void)connHandle; (void)attrHandle; (void)isRead;
    Serial.println("[BLE] Authorization request - granting");
    return true;
  }
};

// ===========================================================================
// RX characteristic callbacks (host writes commands to the ESP32)
// ===========================================================================
class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String value = c->getValue();
    if (value.length() == 0) return;
    char cmd = value[0];
    Serial.print("[BLE] RX command: ");
    Serial.println(cmd);
    handleCommand(cmd);

    // Echo back so the host knows the command was received
    if (pTxChar && deviceConnected) {
      pTxChar->setValue((const uint8_t*)&cmd, 1);
      pTxChar->notify();
    }
  }
};

static void handleCommand(char cmd) {
  switch (cmd) {
    case CMD_FORWARD: Serial.println("[ACTION] FORWARD");  break;
    case CMD_BACK:    Serial.println("[ACTION] BACK");     break;
    case CMD_STOP:    Serial.println("[ACTION] STOP");     break;
    default:
      Serial.print("[ACTION] Unknown command: ");
      Serial.println(cmd);
  }
}

// ===========================================================================
// Rename characteristic callbacks (host writes a new device name)
// ===========================================================================
class RenameCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String value = c->getValue();
    if (value.length() == 0 || value.length() > MAX_DEVICE_NAME_LEN) {
      Serial.println("[RENAME] Invalid name length");
      if (pTxChar && deviceConnected) {
        char fail = CMD_RENAME_FAIL;
        pTxChar->setValue((const uint8_t*)&fail, 1);
        pTxChar->notify();
      }
      return;
    }

    // Copy into a null-terminated char buffer
    char newName[32] = {0};
    value.toCharArray(newName, sizeof(newName));
    if (!isValidDeviceName(newName, value.length())) {
      Serial.println("[RENAME] Invalid device name");
      if (pTxChar && deviceConnected) {
        char fail = CMD_RENAME_FAIL;
        pTxChar->setValue((const uint8_t*)&fail, 1);
        pTxChar->notify();
      }
      return;
    }

    // Store in Preferences (NVS) so it survives reboot
    preferences.begin(PREF_NAMESPACE, false);  // false = read/write
    const bool saved = preferences.putString(PREF_KEY_NAME, newName) == value.length();
    preferences.end();
    if (!saved) {
      Serial.println("[RENAME] Failed to store name");
      if (pTxChar && deviceConnected) {
        char fail = CMD_RENAME_FAIL;
        pTxChar->setValue((const uint8_t*)&fail, 1);
        pTxChar->notify();
      }
      return;
    }

    // Update in-memory name
    strncpy(deviceName, newName, sizeof(deviceName) - 1);
    deviceName[sizeof(deviceName) - 1] = '\0';

    Serial.print("[RENAME] New name: ");
    Serial.println(deviceName);

    // Confirm to host, then schedule advertising restart with new name
    if (pTxChar && deviceConnected) {
      char ok = CMD_RENAME_OK;
      pTxChar->setValue((const uint8_t*)&ok, 1);
      pTxChar->notify();
    }

    pendingRestart = true;
    renameRestartAt = millis() + 750;
  }
};

// ===========================================================================
// Helpers
// ===========================================================================
static void sendNotify(char cmd) {
  if (!deviceConnected || !pTxChar) return;
  pTxChar->setValue((const uint8_t*)&cmd, 1);
  pTxChar->notify();
  Serial.print("[BLE] TX notify: ");
  Serial.println(cmd);
}

static void blinkLED(uint8_t times, uint16_t ms) {
  if (LED_PIN == 255) return;
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
    digitalWrite(LED_PIN, LOW);
    if (i + 1 < times) delay(ms);
  }
}

static bool isValidDeviceName(const char* name, size_t length) {
  if (!name || length == 0 || length > MAX_DEVICE_NAME_LEN) return false;
  if (name[0] == ' ' || name[length - 1] == ' ') return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char ch = static_cast<unsigned char>(name[i]);
    if (ch < 32 || ch > 126) return false;
  }
  return true;
}

// Restart the device so it boots with the new BLE name.
static void restartWithNewName() {
  Serial.print("[BLE] Restarting with new name: ");
  Serial.println(deviceName);
  delay(100);
  ESP.restart();
}

// ===========================================================================
// setup
// ===========================================================================
void setup() {
  Serial.begin(115200);

  if (LED_PIN != 255) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
  }

  pinMode(BUTTON_FORWARD_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BACK_PIN,    INPUT_PULLUP);

  // --- Load device name from Preferences (NVS) ---
  preferences.begin(PREF_NAMESPACE, true);  // true = read-only
  String savedName = preferences.getString(PREF_KEY_NAME, DEFAULT_DEVICE_NAME);
  preferences.end();
  if (isValidDeviceName(savedName.c_str(), savedName.length())) {
    savedName.toCharArray(deviceName, sizeof(deviceName));
  } else {
    strncpy(deviceName, DEFAULT_DEVICE_NAME, sizeof(deviceName) - 1);
    deviceName[sizeof(deviceName) - 1] = '\0';
  }

  Serial.println();
  Serial.println("=== ArduinoRemote (ESP32-C3) ===");
  Serial.print("Device name: ");
  Serial.println(deviceName);

  // --- BLE ---
  BLEDevice::init(deviceName);
  BLEDevice::setMTU(247);
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  pService = pServer->createService(SERVICE_UUID);

  // RX: host writes commands ('F' forward, 'B' back, 'S' stop)
  pRxChar = pService->createCharacteristic(
      CHARACTERISTIC_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_WRITE_NR);
  pRxChar->setCallbacks(new RxCallbacks());

  // TX: ESP32 notifies host of button presses / echoes / rename status
  pTxChar = pService->createCharacteristic(
      CHARACTERISTIC_TX_UUID,
      BLECharacteristic::PROPERTY_NOTIFY);
  pTxChar->addDescriptor(new BLE2902());

  // RN: host writes a new device name to rename the BLE device
  pRenameChar = pService->createCharacteristic(
      CHARACTERISTIC_RENAME_UUID,
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_WRITE_NR);
  pRenameChar->setCallbacks(new RenameCallbacks());

  pService->start();

  // --- Explicit advertisement data so the device NAME shows in the Bluetooth menu ---
  BLEAdvertisementData advData;
  advData.setFlags(0x06);  // General discoverable + BR/EDR not supported
  advData.setName(deviceName);  // <-- this makes the name appear in the scan list
  advData.setCompleteServices(BLEUUID(String(SERVICE_UUID)));

  BLEAdvertisementData scanData;
  scanData.setName(deviceName);  // name also in scan response (Windows reads this)
  scanData.setCompleteServices(BLEUUID(String(SERVICE_UUID)));

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->setScanResponseData(scanData);
  pAdvertising->setMinInterval(0x20);
  pAdvertising->setMaxInterval(0x40);
  pAdvertising->setMinPreferred(0x06);  // helps iOS connect
  BLEDevice::startAdvertising();

  Serial.print("BLE advertising started. Connect to ");
  Serial.println(deviceName);
  blinkLED(2, 150);
}

// ===========================================================================
// loop
// ===========================================================================
void loop() {
  // Restart advertising after a client disconnects
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Advertising restarted");
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // Restart the device with a new name after a rename
  if (pendingRestart && (int32_t)(millis() - renameRestartAt) >= 0) {
    pendingRestart = false;
    restartWithNewName();
  }

  // Scan buttons with debounce
  uint32_t now = millis();
  checkButton(btnForward, now);
  checkButton(btnBack, now);

  delay(10);
}

// Read one button, debounced, active LOW
static void checkButton(Button& b, uint32_t now) {
  int reading = digitalRead(b.pin);  // LOW = pressed

  if (reading != b.lastState) {
    b.lastDebounceTime = now;
  }

  if ((now - b.lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != b.currentState) {
      b.currentState = reading;
      if (b.currentState == LOW) {
        Serial.print("[BUTTON] ");
        Serial.println(b.name);
        sendNotify(b.cmd);
        handleCommand(b.cmd);
        blinkLED(1, 80);
      }
    }
  }

  b.lastState = reading;
}
