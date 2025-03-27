/*
   M5Stack Atom Lite (M5 C008) with Atom RS232 (M5 K046)
   
   Hardware Configuration:
     - BLE & WiFi AP functionality are provided.
     - RS232 communication uses a secondary hardware serial port (Serial2).
     - RS232 configuration for this combo:
         RS232_BAUD:   9600 baud
         RS232_RX_PIN: G22  (pin number 22)
         RS232_TX_PIN: G19  (pin number 19)
   
   Functionality:
     - All system events (BLE connection, BLE data reception, WiFi client events) 
       are logged into an in‑memory log buffer (logBuffer). No log messages are auto‑printed.
     - A web server (running on the WiFi AP) displays the log buffer and includes a link to clear it.
     - A serial menu is printed every 5 seconds on the Serial monitor to allow:
         (R)ead log – prints the current log buffer,
         (C)lear log – clears the log buffer.
     - Data received via BLE is written out on RS232.
   
   Adjust any of the defined constants below as needed.
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <WebServer.h>

// ----- CONFIGURATION -----
#define ENABLE_AP 1              // Set to 1 to enable the WiFi AP, 0 to disable it.
const char* DEVICE_NAME = "radioTuner";  // Used as BLE device name and WiFi AP SSID.

// RS232 configuration (for Atom RS232 module on M5Stack Atom Lite)
#define RS232_BAUD 9600
#define RS232_RX_PIN 22          // RS232 RX is on pin G22
#define RS232_TX_PIN 19          // RS232 TX is on pin G19

// BLE UART UUIDs
#define SERVICE_UUID        "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000ffe1-0000-1000-8000-00805f9b34fb"

// ----- GLOBAL VARIABLES -----
String logBuffer = "";  // In-memory log buffer

// Create a web server on port 80.
WebServer server(80);

// Create RS232 instance on HardwareSerial port 2.
HardwareSerial RS232(2);

// Logging function: Append messages to logBuffer.
void logMessage(const String &message) {
  logBuffer += message + "\n";
}

// Serial menu: Displays available commands.
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
      // Write the received BLE data to the RS232 port.
      RS232.write((const uint8_t*)rx.c_str(), rx.length());
    }
  }
};

#if ENABLE_AP
// ----- WiFi EVENT HANDLER -----
// Logs WiFi client connection/disconnection events.
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
// Handles the root URL: displays the log buffer and a link to clear it.
void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'><title>Log Buffer</title></head><body>";
  html += "<h1>Log Buffer</h1><pre>" + logBuffer + "</pre>";
  html += "<p><a href=\"/clear\">Clear Log</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// Clears the log buffer and redirects back to the root page.
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
  // Start Serial (for menu interaction) at 115200 baud.
  Serial.begin(115200);
  delay(500);
  logMessage("=== radioTuner BLE UART with Log Buffer and RS232 ===");

  // Initialize RS232 with defined baud rate and pins.
  RS232.begin(RS232_BAUD, SERIAL_8N1, RS232_RX_PIN, RS232_TX_PIN);
  logMessage("RS232 started on pins RX: " + String(RS232_RX_PIN) + ", TX: " + String(RS232_TX_PIN));

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

  // Print the serial menu immediately.
  printMenu();
  lastMenuPrint = millis();
}

// ----- MAIN LOOP -----
void loop() {
#if ENABLE_AP
  server.handleClient();
#endif

  // Process serial menu commands.
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
