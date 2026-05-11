# Task8.1HDAudioProcessing — Voice Activated Lighting

## Project Summary
A voice‑activated lighting and fan simulation system that separates speech recognition and decision logic from hardware control.
The Raspberry Pi captures microphone audio, runs an offline Vosk recogniser, maps recognised phrases to tokens, and decides whether to send commands based on the room light level. The Arduino acts as a BLE peripheral, controls two LEDs, reads an LDR light sensor, and drives a servo to simulate an exhaust fan. The Arduino reports an averaged light value in every status message so the Raspberry Pi can avoid turning lights on when the room is already bright.

---

## Hardware used
- **Pi:** Raspberry Pi running the Vosk Python client.
- **Board:** Arduino Nano 33 IoT
- **Actuators:** 2 x LEDs with 220 Ω resistors.
- **Sensors:** LDR in a voltage divider to A0 (analog input).
- **Fan simulation:** Small hobby servo
- **Other:** Breadboard, jumper wires, USB cable

## Wiring summary
- **Bathroom LED:** Anode → 220 Ω → Arduino D7; Cathode → GND.
- **Hallway LED:** Anode → 220 Ω → Arduino D6; Cathode → GND.
- **LDR voltage divider:** LDR → 5 V; other LDR leg → A0 and to 10 kΩ resistor → GND. Read analog at A0.
- **Servo:** Signal → Arduino D8; V+ → 5 V; GND → common GND with Arduino. 

## How to run
1. **Arduino:** Install ArduinoBLE and Servo via Library Manager. Open voice_control.ino, compile and upload to the Nano 33. Confirm READY appears on Serial Monitor (115200 baud).
2. **Raspberry Pi:** Create and activate venv (python3 -m venv ~/vosk-env, source ~/vosk-env/bin/activate). Then install Python dependencies (pip install vosk sounddevice pyserial bleak). Calibrate Bluetooth and adapter. Then run the python program.
3. **Calibrate light threshold:** Speak or send STATUS_REQ to get STATUS ...;LIGHT:<value>. Note bright vs dim values and set LIGHT_THRESHOLD in the Python script (default 300 is a starting point).

## Files included in this folder
- `Audio_Processing.py` - Raspberry Pi Python program (Vosk + transport wrapper for Serial/BLE).
- `voice_control.ino` - Arduino sketch (BLE peripheral, LDR sampling, LED + servo control).
- `README.md` - this file.  
- `YouTube video link` - https://youtu.be/9GZc3fEtlRw

## Code overview - what each part does
**Raspberry Pi**
- **Dependencies:** vosk (offline ASR), sounddevice (microphone capture), bleak (BLE client), plus asyncio, threading, queue, json, time, glob.
- **Audio capture:** sounddevice.RawInputStream with a minimal callback that pushes raw audio blocks into a queue for the main loop.
- **Recognition:** Vosk KaldiRecognizer consumes audio blocks and returns JSON results; the script extracts text.
- **Command parsing:** parse_command() maps phrases to tokens (exact phrase map + fallback keyword rules).
- **Transport abstraction:** Transport chooses serial if available, otherwise BLE. It exposes: send_token_and_wait_ack(token) whcih sends a token and returns device reply andrequest_status_and_parse_light() which requests STATUS_REQ and parses LIGHT:<value>.
- **BLE handling:** BLETransport runs in a background asyncio loop (so the main audio loop stays synchronous). It scans for a device named VoiceNode, connects, writes to a Command characteristic, and subscribes to Status notifications.
- **Decision logic:** Before sending BATH_ON or HALL_ON, the Pi requests status and checks LIGHT against LIGHT_THRESHOLD. If the room is bright, the Raspberry Pi skips sending the ON token.
- **Logging:** The script prints recognised text, mapped tokens, device replies, and decision messages — useful for demo narration.

**Arduino**
- **Dependencies:** ArduinoBLE (BLE peripheral) and Servo.
- **BLE design:** Exposes a service with two characteristics: Command (write) which central writes tokens like BATH_ON, FAN_SIM_ON, STATUS_REQ and Status (notify/read) which Arduino sends STATUS BATH:...;HALL:...;FAN:...;LIGHT:<value> as notifications.
- **Light sampling:** sampleLight() implements a small moving average buffer to smooth LDR readings and stores lastLightValue.
- **Command handling:** handleToken() toggles LEDs, moves the servo, and responds with ACK ... on Serial; it calls sendStatus() after actions.
- **BLE write handler:** onCmdWritten() reads the written bytes, converts to a token string, and calls handleToken() so BLE and Serial commands are handled identically.
- **Status reporting:** sendStatus() prints to Serial and sends a BLE notification when connected.

## Testing checklist
- **Environment:** Run the Python script from the same venv where vosk, sounddevice, pyserial, and bleak are installed. Add your user to dialout if you need serial access without sudo: sudo usermod -aG dialout $(whoami) and re-login.
- **BLE adapter:** Ensure the Pi Bluetooth adapter is up: sudo rfkill unblock bluetooth and sudo hciconfig hci0 up. Run the small bleak scan test to confirm the Pi sees BLE devices.
- **Arduino:** Upload voice_control_ble.ino and confirm READY on Serial Monitor (115200). Test Serial commands manually: send BATH_ON, BATH_OFF, STATUS_REQ and verify ACKs and STATUS output.
- **End‑to‑end:** Start the Python script, confirm it selects Serial or BLE transport (console prints). Speak a mapped phrase (e.g., “bathroom light on”) and watch the Pi console for recognition, token mapping, status request, and device reply. Demonstrate both blocked and allowed ON cases by showing LIGHT:<value> above and below LIGHT_THRESHOLD. Test fan on / fan off and show servo movement.
- **Calibration:** With the room bright, call STATUS_REQ and note LIGHT value; repeat in dim conditions. Adjust LIGHT_THRESHOLD accordingly.
