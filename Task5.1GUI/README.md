# 5.1P - Making a GUI

## Project Summary
A Raspberry Pi GUI that lets a user switch one of three room lights (Living Room, Bathroom, Closet) on and off. The interface uses Tkinter for the GUI and gpiozero (with RPi.GPIO) to control three LEDs connected to GPIO pins; selecting a radio button turns the corresponding LED on and ensures the other LEDs are off.

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
4. Run the GUI (use sudo if your system requires it for GPIO access): sudo python3 5.1P_GUI.py
5. In the GUI: select a room radio button to turn that room’s LED on (other LEDs will turn off). Use Toggle Selected LED to toggle the currently selected LED. Click Exit to clean up GPIO and close the app.

## Files included in this folder
- `5.1P_GUI.py` — main Python GUI script (Tkinter + gpiozero).
- `README.md` — this file.  
- YouTube video link: https://youtu.be/ZQjfCgY_sCY

## Code overview — what each part does
- **Imports and GPIO mode:** imports tkinter, gpiozero.LED, and RPi.GPIO, and sets GPIO numbering to BCM for consistency with the wiring.
- **Pin mapping:** a PINS dictionary centralises the mapping between room names and BCM pin numbers so you can change wiring without altering logic.
- **LED objects:** gpiozero.LED objects are created for each room and initialised to OFF.
- **GUI layout:** Tkinter radio buttons present the three rooms; a toggle button allows manual toggling of the selected LED; an Exit button calls cleanup and closes the window.
- **Event handlers:** select_room() — called when a radio button is selected; turns the chosen LED ON and all others OFF. ledToggle() — toggles the currently selected LED on/off and updates the button text. close() — turns off all LEDs, calls RPi.GPIO.cleanup(), and destroys the Tkinter window.
- **Window protocol:** win.protocol("WM_DELETE_WINDOW", close) ensures cleanup if the window is closed via the window manager.

## Testing checklist
- Confirm the BCM pin numbers in 5.1P_GUI.py match your physical wiring.
- Start the script and select each radio button; verify only the intended LED lights.
- Use the Toggle Selected LED button to confirm toggling behavior.
- Click Exit and confirm all LEDs turn off and the program exits cleanly.
- If a pin behaves unexpectedly, reboot the Pi or run sudo python3 -c "import RPi.GPIO as GPIO; GPIO.cleanup()" to reset GPIO state.
