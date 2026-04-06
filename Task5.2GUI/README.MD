# 5.2C - Change the Light Intensity Using GUI

## Project Summary
A Raspberry Pi GUI that extends the Task 5.1 controller by adding PWM intensity control for the Living Room light. The interface uses Tkinter for the GUI and gpiozero (with RPi.GPIO) to control three LEDs connected to GPIO pins; radio buttons select a room, the Living Room includes a slider to set brightness (duty cycle), and an Exit button cleans up GPIO on close.

---

## Hardware used
- **Board:** Raspberry Pi 4 Model B
- **Indicators:** 3 x LEDs
- **Other:** 3 × 330 Ω resistors, breadboard, jumper wires, common ground

## Wiring summary
- **Common GND:** - connect all LED cathodes to a Pi GND pin.
- **Living Room LED:** - GPIO BCM 14 → 330 Ω resistor → LED anode.
- **Bathroom LED:** - GPIO BCM 15 → 330 Ω resistor → LED anode.
- **Closet LED:** - GPIO BCM 18 → 330 Ω resistor → LED anode.

## How to run
1. Ensure Raspberry Pi OS is installed and your Pi is up to date.
2. Install required Python packages (if not already present).
3. Save the GUI script as 5.1P_GUI.py in your project folder.
4. Run the GUI (use sudo if your system requires it for GPIO access): sudo python3 5.2C_GUI.py
5. In the GUI: Select a room radio button to mark that room as selected (the toggle button will immediately show the selected room). For Living Room, use the slider to change intensity (0–100% maps to PWM duty cycle 0.0–1.0). Use Toggle Selected LED to toggle the selected room (for Living Room this toggles between 0 and the slider value). Click Exit to clean up GPIO and close the app.

## Files included in this folder
- `5.2C_GUI.py` — main Python GUI script (Tkinter + gpiozero).
- `README.md` — this file.  
- `YouTube video link`: https://youtu.be/UHsh8ykR8Bw 

## Code overview — what each part does
- **Imports and GPIO mode:** imports tkinter, gpiozero.LED, and RPi.GPIO, and sets GPIO numbering to BCM for consistency with the wiring.
- **Pin mapping:** a PINS dictionary centralises the mapping between room names and BCM pin numbers so you can change wiring without altering logic.
- **LED objects:** PWMLED is used for the Living Room (smooth brightness control); LED is used for Bathroom and Closet. All LEDs are initialised to OFF.
- **GUI layout:** a LabelFrame lists the three rooms as radio buttons; a slider (Scale) controls Living Room intensity; a toggle button toggles the selected room; an Exit button performs cleanup.
- **Event handlers:** select_room() — called when a radio button is selected; turns the chosen LED ON (or sets PWM value for Living Room) and turns others OFF; updates the toggle button text immediately to show the selected room. on_intensity_change() — called when the slider moves; maps slider value (0–100) to PWMLED.value (0.0–1.0) and updates the toggle text when Living Room is selected. led_toggle() — toggles the currently selected LED on/off; for Living Room it toggles between 0 and the slider value. close() — turns off all LEDs, calls RPi.GPIO.cleanup(), and destroys the Tkinter window.
- **Window protocol:** win.protocol("WM_DELETE_WINDOW", close) ensures cleanup if the window is closed via the window manager.

## Testing checklist
- Confirm the BCM pin numbers in 5.2C_GUI.py match your physical wiring.
- Start the script and select each radio button; verify the toggle button immediately shows which room is selected.
- For Living Room, move the slider and verify the LED brightness changes smoothly.
- For Bathroom and Closet, verify they behave as on/off when selected and toggled.
- Use the Toggle Selected LED button to confirm toggling behavior for each room.
- Click Exit and confirm all LEDs turn off and the program exits cleanly.
- If a pin behaves unexpectedly, reboot the Pi or run sudo python3 -c "import RPi.GPIO as GPIO; GPIO.cleanup()" to reset GPIO state.
