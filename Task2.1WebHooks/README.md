# 2.1PWebHook - Modular IoT Data Logger

## Project Summary
A Nano 33 IoT reads ambient temperature (DHT22) and light level (LDR voltage divider) and pushes the readings to ThingSpeak every 30 seconds. The sketch prints sensor values to Serial for local verification and uses a local arduino_secrets.h file to store Wi‑Fi and ThingSpeak credentials (do not commit this file).

---

## Hardware used
- **Board:** Arduino Nano 33 IoT
- **Sensors:** DHT22 (tempreature and humidity) and LDR in a voltage divider(light)
- **Other:** 10 kΩ pull‑up for DHT data line, fixed resistor for LDR divider, breadboard, jumper wires, USB cable

## Wiring summary
- **DHT22:** - VCC → 3.3 V; DATA → DHTPIN (D2); GND → GND; 10 kΩ pull‑up between DATA and 3.3 V.
- **LDR:** - LDR → 3.3 V; fixed resistor → GND; midpoint → LDR_PIN (A0).

## How to run
1. Copy arduino_secrets.example.h to arduino_secrets.h and fill in ssid, pass, channelID, and writeAPIKey.
2. In Arduino IDE select Board → Arduino Nano 33 IoT (Mbed) and the correct Port.
3. Open 2.1PWebHook.ino, compile and upload.
4. Open Serial Monitor at 115200 to observe sensor readings and ThingSpeak responses; the sketch writes to ThingSpeak every 30 seconds.

## Files included in this folder
- `2.1PWebHook.ino` — main Arduino sketch.  
- `README.md` — this file.  
- `Task 2.1P - Tinkercad Design.png` — circuit schematic or breadboard photo.  
- YouTube video link: https://youtu.be/2G9IbeZIj6Q

## Code overview — what each part does
- **Inlcudes and secrets:** arduino_secrets.h supplies Wi‑Fi and ThingSpeak credentials; libraries handle Wi‑Fi, ThingSpeak, and DHT.
- **Pin definitions:** DHTPIN and LDR_PIN centralise hardware mapping for easy changes.
- **setup():** Initializes Serial, starts the DHT sensor, connects to Wi‑Fi, and initializes ThingSpeak.
- **loop():** Reads DHT temperature and LDR analog value, maps LDR to a 0–100 brightness scale, prints values, and calls ThingSpeak.writeFields(channelID, writeAPIKey) to push data
- **Timing:** The loop uses a 30 second delay to satisfy DHT22 minimum read interval (~2 seconds) and ThingSpeak write recommendations (≥15 seconds).

## Testing checklist
- Confirm pin mapping in code matches wiring.
- Verify DHT returns numeric temperature (not NaN); if NaN, check pull‑up, wiring, and DHTTYPE.
- Verify LDR raw ADC changes when lighting changes; adjust mapping if polarity is inverted.
- Confirm Wi‑Fi connects (2.4 GHz network) and Serial prints an IP address.
- Confirm ThingSpeak returns 200 after writeFields; if not, verify channelID and writeAPIKey.
