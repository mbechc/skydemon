# skydemon
BLE to RS232 on M5Stack ESP32 devices for Skydemon to send data to Radio or Autopilot

* BLE UART: Logs connect/disconnect and incoming writes, forwarded to RS232.
* RS232 Echo & Logging: Reads on RX (G22), echoes to TX (G19) and logs with millisecond timestamps.
* WiFi AP: Serves logs over HTTP at 192.168.4.1.
* HTTP Endpoints:
** /: Lists up to the last 5 /log_<n>.txt files as download links.
** /download?file=log_<n>.txt: Streams the file as radioTuner_log_<n>.txt.
* Persistent Logs: Each boot writes to /log_<cycle>.txt in SPIFFS, and older logs beyond 5 are pruned on startup.
* Real-time Serial: Every log entry is printed to the Serial monitor immediately.
