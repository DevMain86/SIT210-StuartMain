# 3.2C - MQTT HC-SR04 Gestures

## Project Summary
A Nano 33 IoT reads distance from an HC‑SR04 ultrasonic sensor and classifies simple gestures as wave or pat. When a gesture is detected the board publishes a short message to an MQTT broker topic (ES/Wave or ES/Pat). The board also subscribes to those topics and toggles two LEDs when messages arrive.

---

## Hardware used
- **Board:** Arduino Nano 33 IoT
- **Sensors:** HC‑SR04 ultrasonic distance sensor
- **Indicators:** 2 × LEDs with 220 Ω series resistors
- **Other:** Breadboard, jumper wires, USB cable, common ground

## Wiring summary
- **HC-SR04:** Tring -> D2, Echo -> D3, VCC -> 3.3V, GND -> GND
- **LED:** Digital pin -> 220 Ω resistor -> LED anode, LED cathode → GND rail, LED pins used: D8 and D9

## How to run
1. Copy arduino_secrets.example.h to arduino_secrets.h and fill in WIFI_SSID, WIFI_PASS, and MY_NAME.
2. In Arduino IDE select Board → Arduino Nano 33 IoT (Mbed) and the correct Port.
3. Install libraries WiFiNINA and ArduinoMqttClient via Library Manager if not already installed.
4. Open the project sketch, compile and upload. The sketch uses 115200 baud for Serial output.
5. Open Serial Monitor at 115200 to observe connection messages, gesture detection, publishes, and incoming MQTT messages. Distance readings are throttled to print every 5 seconds by default.
6. Use an MQTT client to publish to ES/Wave or ES/Pat to test remote control of LEDs.

## Files included in this folder
- `3.2CMQTT.ino` — main Arduino sketch.  
- `README.md` — this file.  
- `Task 3.2C - Tinkercad Design.png` — circuit schematic or breadboard photo.  
- YouTube video link: https://youtu.be/BB6T2prDymc

## Code overview — what each part does
- **Inlcudes and secrets:** Includes and libraries - WiFiNINA handles Wi‑Fi connectivity. ArduinoMqttClient provides MQTT client functionality. arduino_secrets.h supplies Wi‑Fi credentials and the user name.
- **Pin definitions:** trigPin and echoPin map the HC‑SR04 to D2 and D3. hallwayLED and bathroomLED map LEDs to D8 and D9.
- **setup():** Initializes Serial, configures pins, connects to Wi‑Fi, connects to the MQTT broker (broker.emqx.io), sets a unique MQTT client id, registers the message callback, subscribes to ES/Wave and ES/Pat, and publishes a startup message.
- **loop():** Keeps the MQTT client alive with mqttClient.poll(). Samples the ultrasonic sensor at a fixed interval using getDistance(). Throttles distance prints to Serial (every 5 seconds) to keep output readable. Calls detectGesture(distance) to classify gestures and publish messages.
- **getDistance:** Triggers the HC‑SR04 and measures echo time with pulseIn, converts time to centimeters, and returns the distance.
- **detectGesture:** Implements a simple state machine: when an object appears it records the start time; when the object is removed it computes hold time and classifies the interaction as PAT (long hold close) or WAVE (short approach and withdraw). It publishes to the appropriate MQTT topic and updates LEDs locally.
- **onMqttMessage callback:** Reads incoming MQTT messages, prints topic and payload to Serial, and toggles LEDs according to the topic (ES/Wave turns LEDs on, ES/Pat turns LEDs off).
- **setLeds helper:** Centralizes LED control and respects wiring polarity so the rest of the code can call setLEDs(true) or setLEDs(false) without worrying about pin logic.
