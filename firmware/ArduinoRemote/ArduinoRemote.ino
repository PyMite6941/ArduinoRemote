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
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <BLECharacteristic.h>
#include <BLE2902.h>

// ===========================================================================
// CONFIG
// ===========================================================================
static const char* DEVICE_NAME = "ArduinoRemote";

// Buttons are active LOW: wire one side of each push-button to GND,
// the other side to the GPIO below (internal pull-up enabled).
static const uint8_t BUTTON_FORWARD_PIN = 8;
static const uint8_t BUTTON_BACK_PIN    = 9;

// Status LED (set to 255 to disable)
static const uint8_t LED_PIN = 10;

// BLE service / characteristic UUIDs (128-bit)
static const char* SERVICE_UUID        = "7f4f1234-1234-1234-1234-123456789abc";
static const char* CHARACTERISTIC_RX_UUID = "7f4f1235-1234-1234-1234-123456789abc"; // write (host -> ESP32)
static const char* CHARACTERISTIC_TX_UUID = "7f4f1236-1234-1234-1234-123456789abc"; // notify (ESP32 -> host)

// Commands sent over BLE
static const char CMD_FORWARD = 'F';
static const char CMD_BACK    = 'B';
static const char CMD_STOP    = 'S';

static const uint32_t DEBOUNCE_MS = 50;

// Forward declaration (used by RxCallbacks before definition)
static void handleCommand(char cmd);

// ===========================================================================
// BLE globals
// ===========================================================================
static BLEServer* pServer = nullptr;
static BLEService* pService = nullptr;
static BLECharacteristic* pRxChar = nullptr;
static BLECharacteristic* pTxChar = nullptr;

static bool deviceConnected = false;
static bool oldDeviceConnected = false;

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

// ===========================================================================
// setup
// ===========================================================================
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(BUTTON_FORWARD_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BACK_PIN,    INPUT_PULLUP);

  Serial.println();
  Serial.println("=== ArduinoRemote (ESP32-C3) ===");
  Serial.print("Device name: ");
  Serial.println(DEVICE_NAME);

  // --- BLE ---
  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  pService = pServer->createService(SERVICE_UUID);

  // RX: host writes commands ('F' forward, 'B' back, 'S' stop)
  pRxChar = pService->createCharacteristic(
      CHARACTERISTIC_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_WRITE_NR);
  pRxChar->setCallbacks(new RxCallbacks());

  // TX: ESP32 notifies host of button presses / echoes commands
  pTxChar = pService->createCharacteristic(
      CHARACTERISTIC_TX_UUID,
      BLECharacteristic::PROPERTY_NOTIFY);
  pTxChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // helps iOS connect
  BLEDevice::startAdvertising();

  Serial.print("BLE advertising started. Connect to ");
  Serial.println(DEVICE_NAME);
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
