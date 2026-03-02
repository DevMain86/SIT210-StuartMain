# 1.1P - Switching ON Lights: Modular Programming

# Task1.1Lights_ON

## Project summary
A simple assisted‑living lighting controller for an Arduino Nano 33 IoT. Press a single push button to turn on the **porch** LED for **30 seconds** and the **hallway** LED for **60 seconds**. The program uses non‑blocking timers and a debounced button so both lights run concurrently and the system remains responsive.

---

## Hardware used
- **Board:** Arduino Nano 33 IoT  
- **Inputs:** 1 × push button (wired to GND, using internal pull‑up)  
- **Outputs:** 2 × LEDs (with 220 Ω resistors)  
- **Other:** Breadboard, jumper wires, USB data cable

---

## Wiring summary
- **Button** — connected between **D2** and **GND**; code uses `INPUT_PULLUP` so the pin reads HIGH when idle and LOW when pressed.  
- **Porch LED** — anode via **220 Ω** resistor to **D5**, cathode to **GND**.  
- **Hallway LED** — anode via **220 Ω** resistor to **D6**, cathode to **GND**.  
- Ensure **Arduino GND** is connected to the breadboard ground rail so all components share a common ground.

---

## How to run
1. In Arduino IDE select **Board → Arduino Nano 33 IoT (Mbed)** and the correct **Port**.  
2. Copy `Task1.1Lights_ON.ino` into the IDE and upload.  
3. Press the push button: porch LED lights for ~30 s; hallway LED lights for ~60 s. Pressing again restarts both timers.

---

## Files included in this folder
- `Task1.1Lights_ON.ino` — main Arduino sketch.  
- `README.md` — this file.  
- `Tinkercad Design.png` — circuit schematic or breadboard photo.  
- YouTube video link: https://youtu.be/l0gsPbdE8-k

---

## Code overview — what each part does
- **Pin and timing constants** — central place for pin numbers and durations so hardware or timing can be changed quickly.  
- **setupPins()** — configures pin modes and initial output states; isolates hardware setup from logic.  
- **readButtonDebounced()** — performs input debouncing and reports a single press event; keeps input filtering separate from action logic.  
- **activatePorch() / activateHall()** — start each light by setting the output HIGH and computing the future turn‑off time; encapsulates activation behavior.  
- **updateTimers()** — checks current time against stored end times and turns LEDs off when their timers expire; implements non‑blocking timing so both lights can run concurrently.  
- **loop()** — polls the debounced button and calls activation functions on a valid press; always calls `updateTimers()` to maintain timing.

---

## Why modular programming was useful for this task
Modularisation separated concerns into input handling, timing, activation, and hardware setup. This made it straightforward to test each part independently: the timing logic in `updateTimers()` was verified by manually setting end times, and the button logic was validated by isolating `readButtonDebounced()`. Because functions have single responsibilities, replacing the button with a presence sensor or adding a relay for mains control requires changes only in the relevant module, not across the whole program. This reduced debugging time and made the code easier to document and extend for future features.

---

## Design considerations and limitations
- **Button behavior:** current implementation restarts both timers on every press. If you prefer a single press to be ignored while lights are active, change the press handler to only activate when both lights are off.  
- **Power loss:** timers reset on power loss. If persistence across power cycles is required, add nonvolatile storage and safe state recovery.  
- **Scaling to mains lighting:** do not connect mains directly to the Arduino. Use a properly rated relay or SSR with isolation and follow electrical safety standards.  
- **Accessibility:** consider larger tactile switches or audible feedback for users with limited dexterity or vision.

---

## Testing checklist
- Confirm **pin mapping** in code matches wiring.  
- Verify each LED lights when its pin is toggled in a simple blink sketch.  
- Upload the final sketch and press the button; measure that porch ≈ 30 s and hallway ≈ 60 s.  
- Test repeated presses to confirm restart behavior or change logic if you want presses ignored while active.

---

## Safety note
This project controls low‑voltage LEDs only. For real household lighting use a correctly rated relay or SSR with galvanic isolation and follow local electrical codes. Never wire mains directly to the Arduino or breadboard.

