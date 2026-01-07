#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include "BLEHIDDevice.h"
#include "HIDKeyboardTypes.h"

// =========================================================================
// MANUAL KEY DEFINITIONS (To avoid library version conflicts)
// =========================================================================
// These definitions are for Consumer Control (Media Keys)
#define HID_USAGE_CONSUMER         0x0C
#define HID_USAGE_CONSUMER_PLAY_PAUSE  0xCD
#define HID_USAGE_CONSUMER_SCAN_NEXT   0xB5
#define HID_USAGE_CONSUMER_SCAN_PREV   0xB6
#define HID_USAGE_CONSUMER_MUTE        0xE2
#define HID_USAGE_CONSUMER_VOL_UP      0xE9
#define HID_USAGE_CONSUMER_VOL_DOWN    0xEA

// =========================================================================
// WiFi & Web Server Configuration
// =========================================================================
const char* ssid = "ESP32-HID-Controller";
const char* password = "password123";

AsyncWebServer server(80);

// =========================================================================
// BLE HID Configuration
// =========================================================================
BLEHIDDevice* hid;
BLECharacteristic* inputKeyboard;
BLECharacteristic* inputMediaKeys; // Characteristic for media keys
bool deviceConnected = false;

// --- HTML Web Page Content (stored in Flash memory) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 HID Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; background: #f4f4f9; text-align: center; }
    h1 { color: #3f51b5; }
    .button { display: inline-block; background: #4CAF50; color: white; padding: 15px 32px; margin: 4px 2px; border: none; border-radius: 8px; cursor: pointer; font-size: 16px; transition: all 0.3s; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
    .button:hover { background: #45a049; }
    .button-media { background: #008CBA; }
    .button-media:hover { background: #007399; }
    .button-action { background: #f44336; }
    .button-action:hover { background: #d32f2f; }
    .status { font-size: 18px; color: #333; margin: 20px; padding: 10px; border-radius: 5px; background: #e0e0e0; }
  </style>
</head>
<body>
  <h1>ESP32 BlueDucky Controller</h1>
  <p class="status" id="status">Connecting...</p>
  <button class="button" onclick="sendPayload('open_youtube')">Open YouTube</button>
  <button class="button" onclick="sendPayload('open_spotify')">Open Spotify</button>
  <p>Media Controls</p>
  <button class="button button-media" onclick="sendPayload('play_pause')">Play/Pause</button>
  <button class="button button-media" onclick="sendPayload('next_track')">Next Track</button>
  <p>Actions</p>
  <button class="button button-action" onclick="sendPayload('curl_example')">Run cURL</button>
  <button class="button button-action" onclick="sendPayload('skip_ad')">Skip Ad</button>

  <script>
    function updateStatus() {
      fetch('/status').then(r => r.text()).then(connected => {
        document.getElementById('status').innerText = connected === 'true' ? '✅ Target Connected' : '⏳ Waiting for Target...';
      });
    }
    updateStatus(); setInterval(updateStatus, 5000);
    function sendPayload(payloadName) {
      document.getElementById('status').innerText = '⚡ Executing...';
      fetch('/execute', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({payload: payloadName}) })
      .then(r => { if(!r.ok) throw Error(); document.getElementById('status').innerText = '✅ Success!'; setTimeout(updateStatus, 1500); })
      .catch(e => { document.getElementById('status').innerText = '❌ Error!'; setTimeout(updateStatus, 1500); });
    }
  </script>
</body>
</html>
)rawliteral";

// --- BLE Server Callbacks ---
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { deviceConnected = true; Serial.println("✅ Target Connected!"); }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("❌ Target Disconnected. Restarting advertising.");
    pServer->getAdvertising()->start();
  }
};

// =========================================================================
// HID Emulation Functions
// =========================================================================
void typeString(const char* text) {
  if (!deviceConnected) return;
  for (int i = 0; text[i] != '\0'; i++) {
    char c = text[i];
    uint8_t keycode = 0, modifier = 0;
    if (c >= 'a' && c <= 'z') keycode = KEY_A + (c - 'a');
    else if (c >= 'A' && c <= 'Z') { keycode = KEY_A + (c - 'A'); modifier = KEY_LEFT_SHIFT; }
    else if (c >= '1' && c <= '9') keycode = KEY_1 + (c - '1');
    else if (c == '0') keycode = KEY_0;
    else if (c == ' ') keycode = KEY_SPACE;
    else if (c == '.') keycode = KEY_DOT;
    else if (c == '/') keycode = KEY_SLASH;

    if (keycode != 0) {
      uint8_t msg[] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
      inputKeyboard->setValue(msg, sizeof(msg)); inputKeyboard->notify(); delay(12);
      uint8_t msg_release[] = {0, 0, 0, 0, 0, 0, 0, 0};
      inputKeyboard->setValue(msg_release, sizeof(msg_release)); inputKeyboard->notify(); delay(12);
    }
  }
}

void pressKey(uint8_t key, uint8_t modifier = 0) {
  if (!deviceConnected) return;
  uint8_t msg[] = {modifier, 0, key, 0, 0, 0, 0, 0};
  inputKeyboard->setValue(msg, sizeof(msg)); inputKeyboard->notify(); delay(80);
  uint8_t msg_release[] = {0, 0, 0, 0, 0, 0, 0, 0};
  inputKeyboard->setValue(msg_release, sizeof(msg_release)); inputKeyboard->notify();
}

// =========================================================================
// Payload Functions
// =========================================================================
void payload_openApp(const char* appName) { pressKey(KEY_LEFT_GUI); delay(500); typeString(appName); delay(300); pressKey(KEY_ENTER); }
void payload_customCurl(const char* command) { payload_openApp("Termux"); delay(1500); typeString("curl "); typeString(command); pressKey(KEY_ENTER); }
void payload_autoSkipAd() { delay(8000); pressKey(KEY_TAB); delay(100); pressKey(KEY_TAB); delay(100); pressKey(KEY_ENTER); }

void payload_mediaControl(const char* action) {
  if (!deviceConnected) return;
  uint16_t usageCode = 0;
  if (strcmp(action, "play_pause") == 0) usageCode = HID_USAGE_CONSUMER_PLAY_PAUSE;
  else if (strcmp(action, "next") == 0) usageCode = HID_USAGE_CONSUMER_SCAN_NEXT;

  if (usageCode != 0) {
    uint8_t msg[2] = { (uint8_t)(usageCode & 0xFF), (uint8_t)((usageCode >> 8) & 0xFF) };
    inputMediaKeys->setValue(msg, sizeof(msg)); inputMediaKeys->notify(); delay(80);
    uint8_t msg_release[2] = {0, 0};
    inputMediaKeys->setValue(msg_release, sizeof(msg_release)); inputMediaKeys->notify();
  }
}

// =========================================================================
// Main Setup & Loop
// =========================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 BlueDucky (FINAL DEBUG) ---");

  // --- WiFi & Web Server ---
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP: "); Serial.println(IP);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/plain", String(deviceConnected ? "true" : "false")); });
  server.on("/execute", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->contentLength() == 0) { request->send(400); return; }
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, request->arg("plain"));
    if (error || !doc.containsKey("payload")) { request->send(400); return; }
    const char* payloadName = doc["payload"];
    if (strcmp(payloadName, "open_youtube") == 0) payload_openApp("YouTube");
    else if (strcmp(payloadName, "open_spotify") == 0) payload_openApp("Spotify");
    else if (strcmp(payloadName, "play_pause") == 0) payload_mediaControl("play_pause");
    else if (strcmp(payloadName, "next_track") == 0) payload_mediaControl("next");
    else if (strcmp(payloadName, "curl_example") == 0) payload_customCurl("https://api.ipify.org?format=json");
    else if (strcmp(payloadName, "skip_ad") == 0) payload_autoSkipAd();
    else { request->send(404); return; }
    request->send(200);
  });
  server.begin();
  Serial.println("HTTP server started.");

  // --- BLE HID Setup (Composite Device) ---
  BLEDevice::init("Bluetooth Keyboard");
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  hid = new BLEHIDDevice(pServer);
  inputKeyboard = hid->inputReport(1); // Report ID 1 for Keyboard
  inputMediaKeys = hid->inputReport(2); // Report ID 2 for Media Keys

  hid->manufacturer()->setValue("ESP32");
  hid->pnp(0x02, 0x46e, 0x001, 0x01);
  hid->hidInfo(0x00, 0x01);

  // A standard, working HID Report Descriptor for Keyboard + Media Keys
  static const uint8_t reportMap[] = {
    // Keyboard Report
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x95, 0x08, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,
    // Media Keys Report
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x10,
    0x0A, 0xB5, 0x01, 0x0A, 0xB6, 0x01, 0x0A, 0xCD, 0x01, 0x0A, 0xE2, 0x01, 0x0A, 0xE9, 0x01, 0x0A, 0xEA, 0x01,
    0x81, 0x00, 0xC0
  };
  hid->reportMap((uint8_t*)reportMap, sizeof(reportMap));
  hid->startServices();

  BLEAdvertising* pAdvertising = pServer->getAdvertising();
  pAdvertising->setAppearance(HID_KEYBOARD);
  pAdvertising->addServiceUUID(hid->hidService()->getUUID());
  pAdvertising->start();
  Serial.println("✅ BLE HID Advertising started.");
}

void loop() { delay(1000); }
