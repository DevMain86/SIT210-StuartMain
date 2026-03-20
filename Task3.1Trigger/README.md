# Task3.1Trigger - Terrarium Sunlight Monitor

## Project Summary
An Arduino Nano 33 IoT reads ambient illuminance from a BH1750 light sensor and displays live values. The sketch debounces threshold crossings to detect SUN START and SUN STOP, records session duration, and sends HTTPS Webhooks to IFTTT to trigger email notifications. Wi‑Fi and IFTTT secrets are stored in a local arduino_secrets.h file.

---

## Hardware used
- **Board:** Arduino Nano 33 IoT
- **Sensors:** BH1750 I2C Light Sensor
- **Display:** UCTRONICS 0.96 Inch OLED Module
- **Other:** Breadboard, jumper wires, USB cable

## Wiring summary
- **BH1750:** - VCC → 3.3 V; GND → GND; SDA → SDA (A4); SCL → SCL (A5).
- **OLED:** - VCC → 3.3 V; GND → GND; SDA → SDA (A4); SCL → SCL (A5).
- Both I²C devices share the same SDA/SCL lines; ensure modules use the same I²C address (commonly 0x3C). Use pull‑ups only if modules require them.

## How to run
1. Copy arduino_secrets.example.h to arduino_secrets.h and fill in WIFI_SSID, WIFI_PASS, IFTTT_KEY, IFTTT_EVENT_START, IFTTT_EVENT_STOP.
2. Install libraries via Library Manager: BH1750, WiFiNINA, Adafruit SSD1306, Adafruit GFX.
3. Open Task3.1Trigger_with_OLED.ino, compile and upload.
4. Open Serial Monitor to view debug output. The OLED shows live lux, state, session duration and Wi‑Fi IP. Trigger emails by crossing the configured SUN_THRESHOLD_LUX.

## Files included in this folder
- `3.1Trigger.ino` — main Arduino sketch.  
- `README.md` — this file.  
- YouTube video link: https://youtu.be/Dc67ItbiGZA

## Code overview — what each part does
- **Inlcudes and secrets:** arduino_secrets.h supplies Wi‑Fi and IFTTT credentials; libraries handle I²C, BH1750, OLED and Wi‑Fi TLS.
- **Configuration constraints:** SUN_THRESHOLD_LUX, READ_INTERVAL_MS, and DEBOUNCE_REQUIRED centralise behaviour and make tuning simple.
- **formatLux():** converts the float lux to a one‑decimal string in a SAMD‑safe way (avoids dtostrf).
- **connectWiFi():** connects to the configured Wi‑Fi network and prints the IP address.
- **sendIFTTT_https():** formats value1 (lux) and value2 (session seconds), builds the Webhooks path, opens a TLS connection with WiFiSSLClient, sends an HTTP GET, and prints the HTTP response for verification.
- **Main loop:** reads BH1750 every READ_INTERVAL_MS, applies threshold comparison and debounce logic, toggles isInSunlight on confirmed changes, records session start/stop times, calls sendIFTTT_https() on START/STOP, and updates the OLED.

## Testing checklist
- **Secrets:** confirm arduino_secrets.h contains correct WIFI_SSID, WIFI_PASS, IFTTT_KEY, and event names that match your IFTTT applets.
- **I2C devices:** run an I²C scanner to verify BH1750 and OLED addresses
- **Serial verification:** open Serial Monitor and confirm Lux: readings, DEBUG luxStr: and DEBUG HTTPS URL: lines appear when events trigger.
- **IFTTT delivery:** after a 200 OK response, check IFTTT → Activity and the recipient email.
- **Debounce tuning:** adjust SUN_THRESHOLD_LUX and DEBOUNCE_REQUIRED to avoid false triggers from brief flickers.
- **Wi‑Fi resilience:** test with Wi‑Fi disconnected and reconnected to ensure the sketch handles skipped sends and reconnect attempts.
