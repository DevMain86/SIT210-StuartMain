# Task4.1Interrupts - Porch/Hallway Light Controller

## Project Summary
An Arduino Nano 33 IoT (or Uno/Nano R3 compatible wiring) uses a PIR motion sensor and a BH1750 light sensor to automatically switch two LEDs (representing porch and hallway lights) ON when motion is detected and it is dark. A slider switch provides a manual override (backup) using an interrupt. The sketch uses hardware interrupts for PIR and slider, reads ambient lux from the BH1750, applies a lux threshold and auto‑off timer, and prints concise status and event messages to the Serial Monitor

---

## Hardware used
- **Board:** Arduino Nano 33 IoT
- **Sensors:** BH1750 I2C Light Sensor
- **Motion:** PIR motion sensor
- **Controls:** Slider switch
- **Indicators:** 2 x LED's with 220 Ω series resistors
- **Other:** Breadboard, jumper wires, USB cable

## Wiring summary
- **BH1750:** - VCC → 3.3 V; GND → GND; SDA → SDA (A4); SCL → SCL (A5).
- **PIR:** - VCC → 5 V; GND → GND; OUT → D2
- **Slider:** one side → D3; other side → GND; configure INPUT_PULLUP in code
- **LED1:** D8 → 220 Ω → LED anode; LED cathode → GND
- **LED2:** D9 → 220 Ω → LED anode; LED cathode → GND

## How to run
1. Open Task4.1Interrupts.ino in Arduino IDE
2. Confirm board selection (Arduino Nano 33 IoT or Uno/Nano R3) and correct COM port.
3. Ensure BH1750 is powered at 3.3 V
4. Upload sketch
5. Open Serial Monitor at 115200 baud to view initialization, PIR/slider events, lux readings, and auto‑off messages
6. Test sequence: startup → toggle slider → cover BH1750 (make dark) → wave at PIR → observe lights ON and Serial messages → wait for auto‑off

## Files included in this folder
- `HandlingInterrupts.ino` — main Arduino sketch.  
- `README.md` — this file.  
- YouTube video link: https://youtu.be/YbwLaQYx3oQ

## Code overview — what each part does
- **Inlcudes and globals:** imports Wire and BH1750; defines pin assignments, volatile ISR flags/counters, runtime state, auto‑off timeout, BH1750 state, and lux threshold.
- **ISR's (PIR and Slider):** minimal interrupt handlers that set flags and increment counters; all heavy work is deferred to loop().
- **setLights(on):** helper to switch both LEDs and update lightsOn state.
- **safeReadLux():** wrapper for BH1750 reads that returns lux or -1.0 on error and marks the sensor unavailable if a read fails.
- **setup():** initializes Serial, pins, BH1750 (I²C), and attaches interrupts 
- **Main loop:** - Periodic debug/status prints. Handles PIR events (reads lux, applies LUX_THRESHOLD, turns lights ON and resets timer if dark). Handles slider events with simple debounce (200 ms) and treats LOW as manual ON (resets timer). Auto‑off logic: turns lights OFF when millis() - lastActivityMillis >= AUTO_OFF_MS. Periodic status print includes lux. Small delay(10) to keep loop responsive without busy‑spinning.

## Testing checklist
- **Serial Monitor:** open at 115200 baud; verify startup messages: BH1750 init, “Setup complete.” and periodic status lines.
- **BH1750 I2C:** run an I2C scanner or confirm Serial shows 
- **PIR Behaviour:** wave in front of PIR; Serial should show PIR triggered.lux=... and, if dark, Motion Detected - lights ON(dark). Adjust PIR sensitivity/delay pots if PIR stays HIGH.
- **SLider behaviour:** toggle slider; Serial should show Slider ON -> lights ON (manual, timer started). and debounce prevents spurious toggles.
- **Auto-off:** confirm lights turn off after AUTO_OFF_MS (default 5000 ms) with Auto-off: no activity -> lights OFF printed. Change AUTO_OFF_MS to tune timeout.
- **BH1750 failure fallback:** if BH1750 read fails, the sketch prints an error and (optionally) treats environment as dark if ASSUME_DARK_IF_NO_SENSOR is true.
