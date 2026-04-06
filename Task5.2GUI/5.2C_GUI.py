from tkinter import*
import tkinter.font
from gpiozero import LED, PWMLED
import RPi.GPIO
RPi.GPIO.setmode(RPi.GPIO.BCM)

# Hardware Pin mapping
PINS = {
	"Living Room": 18, # PWM-capabale pin
	"Bathroom": 15,
	"Closet": 14
}

# Create LED objects
leds = {
	"Living Room": PWMLED(PINS["Living Room"]),
	"Bathroom": LED(PINS["Bathroom"]),
	"Closet": LED(PINS["Closet"])
}

# Ensure all LEDs are off at the start
for l in leds.values():
	l.off()
	
# GUI Setup
win = Tk()
win.title("Home Lights with Intensity")
myFont = tkinter.font.Font(family="Helvetica", size = 12, weight = "bold")

# Selected room variable
var = StringVar(value="") # No selection initially

# Keep track of last living room intensity
last_living_intensity = 0.5

# Event functions
def select_room():
	"""Called when a radio button is selected. Turn on selcted LED and turn others off.
	For living room, apply the slider intensity."""
	room = var.get()
	for name, led in leds.items():
		if name == room:
			if isinstance(led, PWMLED):
				# apply slider value
				val = intensity_scale.get() / 100.0
				led.value = val
			else:
				led.on()
		else:
			# turn others off
			led.off()
	update_toggle_text()
	
def led_toggle():
	"""Toggle the currently selected LED on/off. For PWMLED, toggle between 
	0 and slider value"""
	room = var.get()
	if not room:
		return
	led = leds[room]
	if isinstance(led, PWMLED):
		if led.value > 0:
			# store current intensidty and turn off
			global last_living_intensity
			last_living_intensity = led.value
			led.value = 0
			ledButton["text"] = f"Turn {room} ON"
		else:
			# restore to slider value
			val = intensity_scale.get() / 100.0
			led.value = val
			ledButton["text"] = f"Turn {room} OFF"
	else:
		if led.is_lit:
			led.off()
			ledButton["text"] = f"Turn {room} ON"
		else:
			led.on()
			ledButton["text"] = f"Turn {room} OFF"
			
def on_intensity_change(val):
	"""Called when the intensity slider moves. Apply only if living room is selected"""
	try:
		v = float(val)
	except ValueError:
		return
	intensity_label.config(text=f"{int(v)}%")
	if var.get() == "Living Room":
		leds["Living Room"].value = v / 100.0
		
def update_toggle_text():
	"""Update the toggle button text to reflect the selected room state"""
	room = var.get()
	if not room:
		ledButton["text"] = "Toggle selected LED"
		return
	led = leds[room]
	if isinstance(led, PWMLED):
		if led.value > 0:
			ledButton["text"] = f"Turn {room} OFF"
		else:
			ledButton["text"] = f"Turn {room} ON"
	else:
		ledButton["text"] = f"Turn {room} OFF" if led.is_lit else f"Turn {room} ON"
		
def close():
	"""Cleanup and close the GUI"""
	for l in leds.values():
		l.off()
	RPi.GPIO.cleanup()
	win.destroy()
	
# Widgets
# Radio buttons for rooms
rooms_frame = LabelFrame(win, text = "Rooms", font = myFont, padx = 8, pady = 8)
rooms_frame.grid(row = 0, column = 0, rowspan = 3, sticky = "nw", padx = 10, pady = 10)

for name in PINS.keys():
	rb = Radiobutton(rooms_frame, text = name, variable = var, value = name, command = select_room, font = myFont, anchor = "w")
	rb.pack(anchor = "w", pady = 4)
	
# Toggle button
ledButton = Button(win, text = 'Toggle Selected LED', font = myFont, command = led_toggle, bg = 'bisque2', height = 1, width = 24)
ledButton.grid(row = 0, column = 1, padx = 10, pady = 5, sticky = "n")

# Intensity slider for Living Room (0-100)
intensity_scale = Scale(win, from_ = 0, to = 100, orient = HORIZONTAL, label = "Living Room Intensity", command = on_intensity_change, length = 300)
intensity_scale.set(50) # default 50%
intensity_scale.grid(row = 1, column = 1, padx = 10, pady = 10, sticky = "we")

# Intensity % label
intensity_label = Label(win, text = "50%")
intensity_label.grid(row = 1, column = 2, padx = 5, sticky = "n")

# Exit button
exitButton = Button(win, text = 'Exit', font = myFont, command = close, bg = 'red', height = 1, width = 8)
exitButton.grid(row = 2, column = 1, padx = 10, pady = 5, sticky = "s")

# Ensure cleanup if window closed via windows manager
win.protocol("WM_DELETE_WINDOW", close)

# Start GUI loop
win.mainloop()  	


