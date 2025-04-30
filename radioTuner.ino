/*
  radioTuner: M5Stack Atom Lite + Atom RS232

  Features:
    • BLE UART: logs connect/disconnect & incoming data
    • RS232 (Serial2) echo & logging (RX pin G22, TX pin G19, 9600 baud)
    • WiFi AP only (SSID = DEVICE_NAME) hosting HTTP:
        – GET /                → list of stored logs with download links
        – GET /download?file   → download log as "radioTuner_<name>.txt"
    • Per-boot logs in SPIFFS (/log_<cycle>.txt), retain latest 5
    • Millisecond-precision timestamps: <sec>.<msec> "SRC" "MSG"
    • Real-time Serial output of every log entry

  On future reference, “radioTuner” refers to this complete feature set.
*/

#include <FS.h>
#include <SPIFFS.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <WiFi.h>

#define DEVICE_NAME    "radioTuner"
#define RS232_BAUD     9600
#define RS232_RX_PIN   22
#define RS232_TX_PIN   19
#define SERVICE_UUID   "0000ffe0-0000-1000-8000-00805f9b34fb"
#define CHAR_UUID      "0000ffe1-0000-1000-8000-00805f9b34fb"
#define MAX_LOG_FILES  5

// Globals
String logBuffer;
WiFiServer wifiServer(80);
HardwareSerial RS232(2);
BLECharacteristic* pCharacteristic;
BLEServer*        pServer;
bool              deviceConnected = false;
unsigned long     bootMillis;
uint32_t          cycleId;
String            logFilename;

// Append timestamped entry, print to Serial, buffer, and SPIFFS
void appendLog(const String &src, const String &msg) {
  unsigned long d = millis() - bootMillis;
  unsigned long s = d / 1000;
  unsigned long ms = d % 1000;
  char ts[16];
  snprintf(ts, sizeof(ts), "%lu.%03lu", s, ms);
  String line = String(ts) + " " + src + " \"" + msg + "\"\r\n";
  // Real-time Serial
  Serial.print(line);
  // In-memory tail
  logBuffer += line;
  if (logBuffer.length() > 4096) logBuffer = logBuffer.substring(logBuffer.length() - 4096);
  // SPIFFS
  File f = SPIFFS.open(logFilename, FILE_APPEND);
  if (f) { f.print(line); f.close(); }
}

// Keep only the last MAX_LOG_FILES logs
void pruneOldLogs() {
  std::map<uint32_t, String> files;
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    if (name.startsWith("/log_") && name.endsWith(".txt")) {
      uint32_t id = name.substring(5, name.indexOf('.', 5)).toInt();
      files[id] = name;
    }
    file.close();
    file = root.openNextFile();
  }
  while (files.size() > MAX_LOG_FILES) {
    auto it = files.begin();
    SPIFFS.remove(it->second);
    files.erase(it);
  }
}

// BLE server callbacks
class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    deviceConnected = true;
    appendLog("BLE", "Connected");
  }
  void onDisconnect(BLEServer* s) override {
    deviceConnected = false;
    appendLog("BLE", "Disconnected");
    s->getAdvertising()->start();
    appendLog("BLE", "Advertising");
  }
};
class CharCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* ch) override {
    String rx = ch->getValue();
    if (rx.length()) {
      appendLog("BLE", rx);
      RS232.write((const uint8_t*)rx.c_str(), rx.length());
    }
  }
};

// WiFi AP client events
void WiFiEvent(WiFiEvent_t e) {
  if (e == WIFI_EVENT_AP_STACONNECTED)      appendLog("WIFI", "Client connected");
  else if (e == WIFI_EVENT_AP_STADISCONNECTED) appendLog("WIFI", "Client disconnected");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // SPIFFS mount and prune
  SPIFFS.begin(true);
  pruneOldLogs();

  // Cycle file
  File cf = SPIFFS.open("/cycle.txt", "r");
  cycleId = cf ? cf.parseInt() : 0;
  if (cf) cf.close();
  cycleId++;
  cf = SPIFFS.open("/cycle.txt", "w"); cf.println(cycleId); cf.close();
  logFilename = "/log_" + String(cycleId) + ".txt";
  bootMillis = millis();
  appendLog("SYSTEM", "Boot cycle=" + String(cycleId));

  // RS232 init
  RS232.begin(RS232_BAUD, SERIAL_8N1, RS232_RX_PIN, RS232_TX_PIN);
  appendLog("SYSTEM", "RS232@" + String(RS232_BAUD));

  // WiFi AP
  WiFi.softAP(DEVICE_NAME);
  WiFi.onEvent(WiFiEvent);
  appendLog("WIFI", "AP @" + WiFi.softAPIP().toString());
  wifiServer.begin();
  appendLog("SYSTEM", "HTTP started");

  // OTA via AP
  ArduinoOTA.setHostname(DEVICE_NAME);
  ArduinoOTA.begin();
  appendLog("SYSTEM", "OTA Ready");

  // BLE init
  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  BLEService* svc = pServer->createService(SERVICE_UUID);
  pCharacteristic = svc->createCharacteristic(CHAR_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new CharCallbacks());
  pCharacteristic->setValue("Hello");
  svc->start();
  auto adv = pServer->getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->start();
  appendLog("BLE", "Advertising");
}

void loop() {
  // OTA
  ArduinoOTA.handle();

  // RS232 echo & log
  if (RS232.available()) {
    String rs;
    while (RS232.available()) {
      char b = RS232.read();
      rs += b;
      RS232.write(b);
    }
    appendLog("RS232", rs);
  }

  // HTTP handler
  WiFiClient client = wifiServer.available();
  if (client) {
    String req = client.readStringUntil('\r'); client.read();
    while (client.connected()) {
      String h = client.readStringUntil('\n');
      if (h == "\r" || h.length() == 0) break;
    }
    if (req.startsWith("GET /download?file=")) {
      int p1 = req.indexOf('=');
      int p2 = req.indexOf(' ', p1);
      String fn = req.substring(p1 + 1, p2);
      File dl = SPIFFS.open("/" + fn, "r");
      if (dl) {
        String out = String("radioTuner_") + fn;
        client.printf(
          "HTTP/1.1 200 OK\r\n"
          "Content-Disposition: attachment; filename=\"%s\"\r\n"
          "Content-Type: application/octet-stream\r\n"
          "Content-Length: %u\r\n\r\n",
          out.c_str(), dl.size()
        );
        while (dl.available()) client.write(dl.read());
        dl.close();
      } else {
        client.print("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile not found");
      }
    } else {
      client.print(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<html><body><h1>Stored Logs</h1><ul>"
      );
      uint32_t start = (cycleId > MAX_LOG_FILES ? cycleId - MAX_LOG_FILES + 1 : 1);
      for (uint32_t i = start; i <= cycleId; ++i) {
        String fn = "log_" + String(i) + ".txt";
        if (SPIFFS.exists("/" + fn)) {
          client.print("<li><a href=\"/download?file=" + fn + "\">" + fn + "</a></li>");
        }
      }
      client.print("</ul></body></html>");
    }
    delay(1);
    client.stop();
  }

  delay(10);
}
