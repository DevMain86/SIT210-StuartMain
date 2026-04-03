from tkinter import *
import tkinter.font
from gpiozero import LED
import RPi.GPIO
RPi.GPIO.setmode(RPi.GPIO.BCM)

# Hardware PIN mapping
PINS = {
    "Living Room": 14,
    "Bathroom": 15,
    "Closet": 18
}

#Creat gpiozero LED objects
leds = {name: LED(pin) for name, pin in PINS.items()}

# GUI setup
win = Tk()
win.title("Home Lights Controller")
myFont = tkinter.font.Font(family="Helvetica", size=12, weight="bold")

# Ensure all LEDs are off at the start
for led in leds.values():
    led.off()
    
# Event functions
def select_room():
    """Turn on the selected room LED and turn off others"""
    room = var.get()
    for name, lef in leds.items():
        if name == room:
            led.on()
        else:
            lef.off()
    
    # Update toggle button text to reflect state of selected LED
    if room:
        ledButton["text"] = f"{room} ON"
    else:
        ledButton["text"] = "No room selected"
        
def ledToggle():
    """Toggle the currently selected LED on/off"""
    room = var.get()
    if not room:
        return
    led = leds[room]
    if led.is_lit:
        led.off()
        ledButton["text"] = f"Turn {room} ON"
    else:
        led.on()
        ledButton["text"] = f"Turn {room} OFF"
        
def close():
    """Cleanup GPIO and close the GUI."""
    for led in leds.values():
        led.off()
    RPi.GPIO.cleanup()
    win.destroy()
    
# Widgets
var = StringVar(value="")

row = 0
for name in PINS.keys():
    rb = Radiobutton(win, text=name, variable=var, value=name, command=select_room, font=myFont)
    rb.grid(row=row, column=0, sticky="w", padx=10, pady=5)
    row += 1
    
ledButton = Button(win, text='Toggle Selected LED', font=myFont, command=ledToggle, bg="bisque2", height=1, width=24)
ledButton.grid(row=0, column=1, rowspan=1, padx=10, pady=5)

exitButton = Button(win, text='Exit', font=myFont, command=ledToggle, bg="red", height=1, width=8)
exitButton.grid(row=1, column=1, rowspan=1, padx=10, pady=5)

# Ensure cleanup if window closed via window manager
win.protocol("WM_DELETE_WINDOW", close)
win.mainloop()
