#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "BLEHIDDevice.h"
#include "HIDTypes.h" // Use HIDTypes for more definitions

// =========================================================================
// WiFi & Web Server Configuration
// =========================================================================
const char* ssid = "ESP32-HID-Controller";
const char* password = "password123";

AsyncWebServer server(80);

// =========================================================================
// BLE HID Configuration (Corrected for Composite Device)
// =========================================================================
BLEHIDDevice* hid;
BLECharacteristic* inputKeyboard;
BLECharacteristic* inputConsumer; // For media keys
bool deviceConnected = false;

// --- HTML Web Page Content (stored in Flash memory) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 HID Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html { font-family: Arial, sans-serif; display: inline-block; margin: 0px auto; text-align: center; }
    body { background-color: #f4f4f9; }
    h1 { color: #3f51b5; padding: 2vh; }
    .button { display: inline-block; background-color: #4CAF50; border: none; border-radius: 8px; color: white; padding: 15px 32px; text-decoration: none; font-size: 16px; margin: 4px 2px; cursor: pointer; transition: all 0.3s; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
    .button:hover { background-color: #45a049; box-shadow: 0 6px 8px rgba(0,0,0,0.15); }
    .button-media { background-color: #008CBA; }
    .button-media:hover { background-color: #007399; }
    .button-action { background-color: #f44336; }
    .button-action:hover { background-color: #d32f2f; }
    .container { padding: 20px; }
    .status { font-size: 18px; color: #333; margin: 20px; font-weight: bold; padding: 10px; border-radius: 5px; background-color: #e0e0e0; }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP32 BlueDucky Controller</h1>
    <p class="status" id="status">Connecting to ESP32...</p>
    
    <button class="button" onclick="sendPayload('open_youtube')">Open YouTube</button>
    <button class="button" onclick="sendPayload('open_spotify')">Open Spotify</button>
    
    <p style="margin-top: 20px;">Media Controls</p>
    <button class="button button-media" onclick="sendPayload('play_pause')">Play/Pause</button>
    <button class="button button-media" onclick="sendPayload('next_track')">Next Track</button>
    
    <p style="margin-top: 20px;">Actions</p>
    <button class="button button-action" onclick="sendPayload('curl_example')">Run cURL</button>
    <button class="button button-action" onclick="sendPayload('skip_ad')">Skip Ad</button>
  </div>

  <script>
    function updateStatus() {
      fetch('/status')
        .then(response => response.text())
        .then(isConnected => {
          document.getElementById('status').innerText = isConnected === 'true' ? '✅ Target Connected' : '⏳ Waiting for Target...';
        });
    }

    // Update status on load and then every 5 seconds
    updateStatus();
    setInterval(updateStatus, 5000);

    function sendPayload(payloadName) {
      console.log("Sending payload: " + payloadName);
      document.getElementById('status').innerText = '⚡ Executing...';
      
      fetch('/execute', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ payload: payloadName })
      })
      .then(response => {
        if (!response.ok) throw new Error('Network error');
        console.log('Payload sent!');
        document.getElementById('status').innerText = '✅ Success!';
        setTimeout(updateStatus, 1500); // Revert to connection status
      })
      .catch(error => {
        console.error('Error:', error);
        document.getElementById('status').innerText = '❌ Error!';
        setTimeout(updateStatus, 1500); // Revert to connection status
      });
    }
  </script>
</body>
</html>
)rawliteral";

// --- BLE Server Callbacks ---
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("✅ Target device connected via BLE!");
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("❌ Target device disconnected. Restarting advertising.");
    BLEAdvertising* pAdvertising = pServer->getAdvertising();
    pAdvertising->start();
  }
};

// =========================================================================
// HID Keyboard Emulation Functions
// =========================================================================
void typeString(const char* text) {
  if (!deviceConnected) return;
  Serial.printf("Typing: %s\n", text);
  for (int i = 0; text[i] != '\0'; i++) {
    char c = text[i];
    uint8_t keycode = 0;
    uint8_t modifier = 0;

    if (c >= 'a' && c <= 'z') keycode = KEY_A + (c - 'a');
    else if (c >= 'A' && c <= 'Z') { keycode = KEY_A + (c - 'A'); modifier = KEY_LEFT_SHIFT; }
    else if (c >= '1' && c <= '9') keycode = KEY_1 + (c - '1');
    else if (c == '0') keycode = KEY_0;
    else if (c == ' ') keycode = KEY_SPACE;
    else if (c == '.') keycode = KEY_DOT;
    else if (c == '-') keycode = KEY_MINUS;
    else if (c == '/') keycode = KEY_SLASH;

    if (keycode != 0) {
      uint8_t msg[] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
      inputKeyboard->setValue(msg, sizeof(msg));
      inputKeyboard->notify();
      delay(12);
      uint8_t msg_release[] = {0, 0, 0, 0, 0, 0, 0, 0};
      inputKeyboard->setValue(msg_release, sizeof(msg_release));
      inputKeyboard->notify();
      delay(12);
    }
  }
}

void pressKey(uint8_t key, uint8_t modifier = 0) {
  if (!deviceConnected) return;
  Serial.printf("Pressing key: %d with modifier: %d\n", key, modifier);
  uint8_t msg[] = {modifier, 0, key, 0, 0, 0, 0, 0};
  inputKeyboard->setValue(msg, sizeof(msg));
  inputKeyboard->notify();
  delay(80); // Slightly longer delay for reliability
  uint8_t msg_release[] = {0, 0, 0, 0, 0, 0, 0, 0};
  inputKeyboard->setValue(msg_release, sizeof(msg_release));
  inputKeyboard->notify();
}

// =========================================================================
// Payload Functions
// =========================================================================
void payload_openApp(const char* appName) {
  Serial.printf("Executing: Open App '%s'\n", appName);
  pressKey(KEY_LEFT_GUI); // Opens app drawer/search
  delay(500);
  typeString(appName);
  delay(300);
  pressKey(KEY_ENTER);
}

void payload_mediaControl(const char* action) {
  Serial.printf("Executing: Media Control '%s'\n", action);
  if (!deviceConnected) return;
  uint16_t usageCode = 0;
  if (strcmp(action, "play_pause") == 0) usageCode = HID_USAGE_CONSUMER_PLAY_PAUSE;
  else if (strcmp(action, "next") == 0) usageCode = HID_USAGE_CONSUMER_SCAN_NEXT;
  // Add other media keys here if needed

  if (usageCode != 0) {
    uint8_t msg[2] = { (uint8_t)(usageCode & 0xFF), (uint8_t)((usageCode >> 8) & 0xFF) };
    inputConsumer->setValue(msg, sizeof(msg));
    inputConsumer->notify();
    delay(80);
    uint8_t msg_release[2] = {0, 0};
    inputConsumer->setValue(msg_release, sizeof(msg_release));
    inputConsumer->notify();
  }
}

void payload_customCurl(const char* command) {
  Serial.printf("Executing: Custom cURL '%s'\n", command);
  payload_openApp("Termux");
  delay(1500);
  typeString("curl ");
  typeString(command);
  pressKey(KEY_ENTER);
}

void payload_autoSkipAd() {
  Serial.println("Executing: Auto-Skip Ad (Fragile!)");
  delay(8000);
  pressKey(KEY_TAB);
  delay(100);
  pressKey(KEY_TAB);
  delay(100);
  pressKey(KEY_ENTER);
}

// =========================================================================
// Main Setup & Loop
// =========================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 BlueDucky Web Controller (DEBUGGED) ---");

  // --- WiFi & Web Server Setup ---
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/plain", String(deviceConnected ? "true" : "false")); });
  
  server.on("/execute", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->contentLength() == 0) { request->send(400, "text/plain", "Error: No body"); return; }
    String body = request->arg("plain");
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, body);
    if (error || !doc.containsKey("payload")) { request->send(400, "text/plain", "Error: Invalid JSON"); return; }

    const char* payloadName = doc["payload"];
    Serial.printf("Executing payload: %s\n", payloadName);

    if (strcmp(payloadName, "open_youtube") == 0) payload_openApp("YouTube");
    else if (strcmp(payloadName, "open_spotify") == 0) payload_openApp("Spotify");
    else if (strcmp(payloadName, "play_pause") == 0) payload_mediaControl("play_pause");
    else if (strcmp(payloadName, "next_track") == 0) payload_mediaControl("next");
    else if (strcmp(payloadName, "curl_example") == 0) payload_customCurl("https://api.ipify.org?format=json");
    else if (strcmp(payloadName, "skip_ad") == 0) payload_autoSkipAd();
    else { request->send(404, "text/plain", "Error: Payload not found"); return; }

    request->send(200, "text/plain", "OK");
  });
  server.begin();
  Serial.println("HTTP server started.");

  // --- BLE HID Setup (Corrected Composite Device) ---
  BLEDevice::init("Bluetooth Keyboard");
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  hid = new BLEHIDDevice(pServer);

  inputKeyboard = hid->inputReport(1); // Report ID 1 for Keyboard
  inputConsumer = hid->inputReport(2); // Report ID 2 for Consumer Control

  hid->manufacturer()->setValue("ESP32");
  hid->pnp(0x02, 0x46e, 0x001, 0x01);
  hid->hidInfo(0x00, 0x01);

  // Combined HID Report Descriptor for Keyboard + Consumer Control
  uint8_t reportMap[] = {
    USAGE_PAGE(1),      0x01,       // Generic Desktop Ctrls
    USAGE(1),           0x06,       // Keyboard
    COLLECTION(1),      0x01,       // Application
    REPORT_ID(1),       0x01,       //   Report ID (1) - Keyboard
    USAGE_PAGE(1),      0x07,       //   Kbrd/Keypad
    USAGE_MINIMUM(1),   0xE0,       //   Keyboard LeftControl
    USAGE_MAXIMUM(1),   0xE7,       //   Keyboard Right GUI
    LOGICAL_MINIMUM(1), 0x00,       //   0
    LOGICAL_MAXIMUM(1), 0x01,       //   1
    REPORT_SIZE(1),     0x01,       //   1 bit
    REPORT_COUNT(1),    0x08,       //   8 bits
    HIDINPUT(1),        0x02,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position
    REPORT_COUNT(1),    0x01,       //   1 byte (Reserved)
    REPORT_SIZE(1),     0x08,       //   8 bits
    HIDINPUT(1),        0x01,       //   Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position
    REPORT_COUNT(1),    0x06,       //   6 bytes (Keys)
    REPORT_SIZE(1),     0x08,       //   8 bits
    LOGICAL_MINIMUM(1), 0x00,       //   0
    LOGICAL_MAXIMUM(1), 0x65,       //   101
    USAGE_PAGE(1),      0x07,       //   Kbrd/Keypad
    USAGE_MINIMUM(1),   0x00,       //   Reserved (no event indicated)
    USAGE_MAXIMUM(1),   0x65,       //   Keyboard Application
    HIDINPUT(1),        0x00,       //   Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position
    END_COLLECTION(0),               // End collection

    USAGE_PAGE(1),      0x0C,       // Consumer
    USAGE(1),           0x01,       // Consumer Control
    COLLECTION(1),      0x01,       // Application
    REPORT_ID(2),       0x02,       //   Report ID (2) - Consumer Control
    LOGICAL_MINIMUM(1), 0x00,       //   0
    LOGICAL_MAXIMUM(1), 0x01,       //   1
    REPORT_SIZE(1),     0x01,       //   1 bit
    REPORT_COUNT(1),    0x10,       //   16 bits
    USAGE(1),           0xB5,       //   Scan Next Track
    USAGE(1),           0xB6,       //   Scan Previous Track
    USAGE(1),           0xCD,       //   Play/Pause
    USAGE(1),           0xE2,       //   Mute
    USAGE(1),           0xE9,       //   Volume Up
    USAGE(1),           0xEA,       //   Volume Down
    HIDINPUT(1),        0x00,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position
    END_COLLECTION(0)               // End collection
  };
  hid->reportMap(reportMap, sizeof(reportMap));
  hid->startServices();

  BLEAdvertising* pAdvertising = pServer->getAdvertising();
  pAdvertising->setAppearance(HID_KEYBOARD);
  pAdvertising->addServiceUUID(hid->hidService()->getUUID());
  pAdvertising->start();
  Serial.println("✅ BLE HID Advertising started.");
}

void loop() {
  // Handled by async web server and BLE callbacks
  delay(1000);
}
