#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <WebServer.h>

// ----- CONFIGURATION -----
#define ENABLE_AP 1              // Set to 1 to enable WiFi AP, 0 to disable it.
const char* DEVICE_NAME = "radioTuner";  // Used as BLE device name and WiFi AP SSID.

// BLE UART UUIDs
#define SERVICE_UUID        "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

// ----- GLOBAL VARIABLES -----
String logBuffer = "";  // In-memory log buffer

// Web server running on port 80.
WebServer server(80);

// Logging function: appends messages to the logBuffer.
void logMessage(const String &message) {
  logBuffer += message + "\n";
}

// Serial menu: displays available commands.
void printMenu() {
  Serial.println("\nSerial Menu: (R)ead log, (C)lear log");
  Serial.print(">");
}

// ----- BLE GLOBAL OBJECTS -----
BLECharacteristic* pCharacteristic;
BLEServer* pServer;
bool deviceConnected = false;

// ----- BLE CALLBACKS -----
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    logMessage("[BLE] Device connected");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    logMessage("[BLE] Device disconnected");
    pServer->getAdvertising()->start();
    logMessage("[BLE] Advertising restarted");
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String rx = pCharacteristic->getValue();
    if (rx.length() > 0) {
      logMessage("[BLE] Received: " + rx);
    }
  }
};

#if ENABLE_AP
// ----- WiFi EVENT HANDLER -----
void WiFiEvent(WiFiEvent_t event) {
  switch(event) {
    case WIFI_EVENT_AP_STACONNECTED:
      logMessage("[WiFi] Client connected");
      break;
    case WIFI_EVENT_AP_STADISCONNECTED:
      logMessage("[WiFi] Client disconnected");
      break;
    default:
      break;
  }
}
#endif

// ----- WEB SERVER HANDLERS -----
// Displays the log buffer and a link to clear it.
void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'><title>Log Buffer</title></head><body>";
  html += "<h1>Log Buffer</h1><pre>" + logBuffer + "</pre>";
  html += "<p><a href=\"/clear\">Clear Log</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// Clears the log buffer and redirects back to root.
void handleClear() {
  logBuffer = "";
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");  
}

// Global timer for printing the serial menu.
unsigned long lastMenuPrint = 0;
const unsigned long menuInterval = 5000; // 5 seconds

// ----- SETUP -----
void setup() {
  // Start Serial for menu interaction.
  Serial.begin(115200);
  delay(500);
  
  logMessage("=== radioTuner BLE UART with Log Buffer ===");
  
#if ENABLE_AP
  logMessage("Starting WiFi AP...");
  WiFi.softAP(DEVICE_NAME);
  WiFi.onEvent(WiFiEvent);
  IPAddress IP = WiFi.softAPIP();
  logMessage("AP started with IP: " + IP.toString());
  
  // Setup web server routes.
  server.on("/", handleRoot);
  server.on("/clear", handleClear);
  server.begin();
  logMessage("[Web] Server started");
#endif
  
  // ----- BLE INITIALIZATION -----
  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ   |
    BLECharacteristic::PROPERTY_WRITE  |
    BLECharacteristic::PROPERTY_WRITE_NR |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->setValue("Hello from ESP32");
  pService->start();
  
  BLEAdvertising* pAdvertising = pServer->getAdvertising();
  BLEAdvertisementData advData;
  advData.setName(DEVICE_NAME);
  advData.setCompleteServices(BLEUUID(SERVICE_UUID));
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->start();
  logMessage("[BLE] Advertising as '" + String(DEVICE_NAME) + "'");
  
  // Print the Serial menu immediately.
  printMenu();
  lastMenuPrint = millis();
}

// ----- MAIN LOOP -----
void loop() {
#if ENABLE_AP
  server.handleClient();
#endif
  
  // Check for serial input for the menu commands.
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'R' || cmd == 'r') {
      Serial.println("\n--- Log Start ---");
      Serial.println(logBuffer);
      Serial.println("--- Log End ---");
      printMenu();
    } else if (cmd == 'C' || cmd == 'c') {
      logBuffer = "";
      Serial.println("\nLog cleared.");
      printMenu();
    }
  }
  
  // Every 5 seconds, re-print the serial menu.
  if (millis() - lastMenuPrint >= menuInterval) {
    printMenu();
    lastMenuPrint = millis();
  }
  
  delay(10);
}
