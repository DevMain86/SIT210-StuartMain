import queue
import sounddevice as sd
import json
import serial
import glob
import time
import asyncio
import threading
from vosk import Model, KaldiRecognizer

try:
    from bleak import BleakScanner, BleakClient
    BLE_AVAILABLE = True
except Exception:
    BLE_AVAILABLE = False


# Configuration

MODEL_PATH = "vosk-model-small-en-us-0.15"  # path to Vosk model
SAMPLE_RATE = 16000                         # microphone sample rate (Hz)
SERIAL_PORT = None                          # set to "/dev/ttyACM0" to force serial
SERIAL_BAUD = 115200                        # must match Arduino Serial.begin()
LIGHT_THRESHOLD = 300                       # LDR threshold (0-1023) to decide "bright"

# BLE identifiers (must match the Arduino BLE sketch)
BLE_SERVICE_NAME = "VoiceNode"
BLE_CMD_UUID = "12345678-1234-5678-1234-56789abcdef1"    # write-only command characteristic
BLE_STATUS_UUID = "12345678-1234-5678-1234-56789abcdef2" # notify/read status characteristic

# Phrase-to-token mapping (exact phrases)
PHRASE_MAP = {
    "bathroom light on": "BATH_ON",
    "bathroom light off": "BATH_OFF",
    "hallway light on": "HALL_ON",
    "hallway light off": "HALL_OFF",
    "fan on": "FAN_SIM_ON",
    "fan off": "FAN_SIM_OFF",
    "status": "STATUS_REQ"
}

# Thread-safe queue used by the audio callback to pass raw audio blocks to the recogniser loop
q = queue.Queue()


# Audio callback for sounddevice

def audio_callback(indata, frames, time, status):
    """
    sounddevice callback invoked in a separate thread.
    It receives raw audio frames and pushes them into a queue for the main loop to process.
    Keep this callback minimal and non-blocking.
    """
    if status:
        print("Audio status:", status)
    q.put(bytes(indata))


# Command parsing

def parse_command(text):
    """
    Convert recognised text into a token the Arduino understands.
    1. Try exact phrase matches from PHRASE_MAP.
    2. Fallback to simple keyword rules (robust to partial recognition).
    Returns a token string (e.g., "BATH_ON") or None if no match.
    """
    text = text.lower()
    for phrase, token in PHRASE_MAP.items():
        if phrase in text:
            return token
    # fallback rules for partial matches
    if "bath" in text and "on" in text:
        return "BATH_ON"
    if "bath" in text and "off" in text:
        return "BATH_OFF"
    if "hall" in text and "on" in text:
        return "HALL_ON"
    if "hall" in text and "off" in text:
        return "HALL_OFF"
    if "fan" in text and "on" in text:
        return "FAN_SIM_ON"
    if "fan" in text and "off" in text:
        return "FAN_SIM_OFF"
    return None


# Serial transport helpers

def find_serial_port():
    """
    Auto-detect a serial device node if SERIAL_PORT is not forced.
    Looks for /dev/ttyACM* and /dev/ttyUSB* and returns the first match.
    """
    if SERIAL_PORT:
        return SERIAL_PORT
    candidates = glob.glob('/dev/ttyACM*') + glob.glob('/dev/ttyUSB*')
    return candidates[0] if candidates else None

def open_serial(port):
    """
    Open a pyserial Serial instance and flush initial data.
    Returns the serial object or None on failure.
    """
    try:
        ser = serial.Serial(port, SERIAL_BAUD, timeout=1)
        time.sleep(0.2)           # allow Arduino to reset/print READY
        ser.reset_input_buffer()  # clear any startup text
        print("Serial opened on", port)
        return ser
    except Exception as e:
        print("Serial not opened:", e)
        return None

def serial_send_and_wait(ser, token, wait_ms=800):
    """
    Send a token over serial and wait up to wait_ms for a single-line reply.
    Returns the reply string or None if no reply.
    """
    try:
        ser.write((token + "\n").encode('utf-8'))
    except Exception as e:
        print("Serial write error:", e)
        return None
    deadline = time.time() + (wait_ms / 1000.0)
    reply = ""
    while time.time() < deadline:
        try:
            line = ser.readline().decode('utf-8').strip()
        except Exception:
            line = ""
        if line:
            reply = line
            break
    return reply if reply else None


# BLE transport helpers (asynchronous wrapped for synchronous use)

class BLETransport:
    """
    Encapsulates BLE operations using bleak.
    Runs inside an asyncio loop (the loop is created and run in a background thread).
    - scan_and_connect: find device by name and connect
    - write_cmd: write a token to the command characteristic and optionally wait for a status notification
    - read_status: read or wait for the status characteristic (returns the status string)
    The class uses a threading.Event to bridge notifications into synchronous code.
    """
    def __init__(self, loop):
        self.loop = loop
        self.client = None
        self.device = None
        self.status_event = threading.Event()
        self.last_status = None
        self.last_light = None

    async def scan_and_connect(self, name_filter=BLE_SERVICE_NAME, timeout=8.0):
        """
        Scan for a BLE device with the given name and connect.
        After connecting, subscribe to notifications on the status characteristic.
        """
        print("Scanning for BLE device named:", name_filter)
        device = await BleakScanner.find_device_by_filter(lambda d, ad: d.name == name_filter, timeout=timeout)
        if not device:
            print("BLE device not found")
            return False
        self.device = device
        print("Found BLE device:", device.address)
        self.client = BleakClient(device)
        try:
            await self.client.connect()
            print("BLE connected to", device.address)
            # subscribe to status notifications
            await self.client.start_notify(BLE_STATUS_UUID, self._status_notify_handler)
            return True
        except Exception as e:
            print("BLE connect/start_notify failed:", e)
            return False

    def _status_notify_handler(self, sender, data):
        """
        Callback invoked by bleak when a notification arrives on the status characteristic.
        Stores the last status and extracts the LIGHT value if present.
        Signals waiting threads via status_event.
        """
        try:
            text = data.decode('utf-8', errors='ignore').strip()
        except Exception:
            text = None
        if text:
            self.last_status = text
            # parse LIGHT:<value> if present
            try:
                if "LIGHT:" in text:
                    parts = text.split(';')
                    for p in parts:
                        if p.startswith("LIGHT:"):
                            v = int(p.split(':',1)[1])
                            self.last_light = v
            except Exception:
                pass
            # notify any waiting synchronous code
            self.status_event.set()

    async def write_cmd(self, token):
        """
        Write a token to the command characteristic.
        After writing, wait briefly for a status notification (if Arduino sends one).
        Returns the last_status string (may be None).
        """
        if not self.client or not self.client.is_connected:
            return None
        try:
            await self.client.write_gatt_char(BLE_CMD_UUID, token.encode('utf-8'))
            # clear and wait for a notification (non-blocking)
            self.status_event.clear()
            try:
                await asyncio.wait_for(self._wait_for_status(), timeout=1.0)
            except asyncio.TimeoutError:
                pass
            return self.last_status
        except Exception as e:
            print("BLE write error:", e)
            return None

    async def read_status(self):
        """
        Try to read the status characteristic directly. If read fails, wait for a notification.
        Returns the status string or None.
        """
        if not self.client or not self.client.is_connected:
            return None
        try:
            data = await self.client.read_gatt_char(BLE_STATUS_UUID)
            text = data.decode('utf-8', errors='ignore').strip()
            self.last_status = text
            if "LIGHT:" in text:
                try:
                    parts = text.split(';')
                    for p in parts:
                        if p.startswith("LIGHT:"):
                            self.last_light = int(p.split(':',1)[1])
                except Exception:
                    pass
            return text
        except Exception:
            # fallback: wait for a notification
            self.status_event.clear()
            try:
                await asyncio.wait_for(self._wait_for_status(), timeout=1.0)
            except asyncio.TimeoutError:
                pass
            return self.last_status

    async def _wait_for_status(self):
        """Async wrapper that polls the threading.Event until set."""
        while not self.status_event.is_set():
            await asyncio.sleep(0.05)
        return True

    def disconnect_sync(self):
        """Disconnect the BLE client from the background thread synchronously."""
        if self.client and self.client.is_connected:
            coro = self.client.disconnect()
            fut = asyncio.run_coroutine_threadsafe(coro, self.loop)
            try:
                fut.result(timeout=2.0)
            except Exception:
                pass


# Unified transport wrapper

class Transport:
    """
    Provides a single interface for the rest of the program to:
      - send_token_and_wait_ack(token)
      - request_status_and_parse_light()
    The Transport will prefer serial if available; otherwise it will attempt BLE.
    BLE is run on a background asyncio loop and wrapped to appear synchronous.
    """
    def __init__(self):
        self.ser = None
        self.ble = None
        self.ble_loop = None
        self.ble_thread = None

    def start(self):
        """
        Start the transport:
        1. Try to open a serial port (USB) first.
        2. If serial not found and bleak is available, start BLE background loop and connect.
        """
        port = find_serial_port()
        if port:
            self.ser = open_serial(port)
            if self.ser:
                return
        # No serial found, try BLE
        if BLE_AVAILABLE:
            # create an asyncio loop in a background thread for bleak
            self.ble_loop = asyncio.new_event_loop()
            self.ble = BLETransport(self.ble_loop)
            def _ble_thread_fn(loop, ble):
                asyncio.set_event_loop(loop)
                loop.run_forever()
            self.ble_thread = threading.Thread(target=_ble_thread_fn, args=(self.ble_loop, self.ble), daemon=True)
            self.ble_thread.start()
            # schedule scan_and_connect on the background loop
            fut = asyncio.run_coroutine_threadsafe(self.ble.scan_and_connect(), self.ble_loop)
            try:
                ok = fut.result(timeout=12.0)
                if not ok:
                    print("BLE connect failed")
                    self.ble = None
                else:
                    print("Using BLE transport")
            except Exception as e:
                print("BLE scan/connect error:", e)
                self.ble = None
        else:
            print("BLE not available (bleak not installed) and no serial device found.")

    def stop(self):
        """Cleanly stop and close serial and BLE resources."""
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        if self.ble:
            self.ble.disconnect_sync()
        if self.ble_loop:
            self.ble_loop.call_soon_threadsafe(self.ble_loop.stop)
        if self.ble_thread:
            self.ble_thread.join(timeout=1.0)

    def send_token_and_wait_ack(self, token, wait_ms=800):
        """
        Send a token to the device and return the device reply.
        Uses serial if available, otherwise BLE.
        """
        if self.ser:
            return serial_send_and_wait(self.ser, token, wait_ms=wait_ms)
        if self.ble and self.ble_loop:
            coro = self.ble.write_cmd(token)
            fut = asyncio.run_coroutine_threadsafe(coro, self.ble_loop)
            try:
                return fut.result(timeout=(wait_ms/1000.0)+0.5)
            except Exception:
                return None
        return None

    def request_status_and_parse_light(self):
        """
        Request STATUS_REQ and parse LIGHT:<value>.
        Returns (reply_string, light_value) or (None, None).
        Works for both serial and BLE transports.
        """
        # Serial path
        if self.ser:
            reply = serial_send_and_wait(self.ser, "STATUS_REQ", wait_ms=1000)
            if not reply:
                return None, None
            light_val = None
            try:
                if reply.startswith("STATUS"):
                    parts = reply.split(None, 1)
                    if len(parts) > 1:
                        kvs = parts[1].split(';')
                        for kv in kvs:
                            if kv.startswith("LIGHT:"):
                                try:
                                    light_val = int(kv.split(':',1)[1])
                                except ValueError:
                                    light_val = None
            except Exception as e:
                print("Error parsing status:", e)
            return reply, light_val

        # BLE path
        if self.ble and self.ble_loop:
            coro = self.ble.read_status()
            fut = asyncio.run_coroutine_threadsafe(coro, self.ble_loop)
            try:
                reply = fut.result(timeout=1.5)
            except Exception:
                reply = None
            light_val = None
            if reply:
                try:
                    if reply.startswith("STATUS"):
                        parts = reply.split(None, 1)
                        if len(parts) > 1:
                            kvs = parts[1].split(';')
                            for kv in kvs:
                                if kv.startswith("LIGHT:"):
                                    try:
                                        light_val = int(kv.split(':',1)[1])
                                    except ValueError:
                                        light_val = None
                except Exception as e:
                    print("Error parsing BLE status:", e)
            return reply, light_val

        return None, None


# Main program

def main():
    """
    Main entry point:
    - load Vosk model and create recognizer
    - start transport (serial or BLE)
    - open microphone stream and process audio blocks
    - when a command is recognized, optionally request light level and decide whether to send ON tokens
    """
    model = Model(MODEL_PATH)
    rec = KaldiRecognizer(model, SAMPLE_RATE)

    transport = Transport()
    transport.start()
    if not transport.ser and not transport.ble:
        print("Warning: no transport available. You can still test recognition locally.")

    try:
        # Open the microphone stream. The callback pushes raw audio into the queue.
        with sd.RawInputStream(samplerate=SAMPLE_RATE, blocksize=8000, dtype='int16',
                               channels=1, callback=audio_callback):
            print("Listening...")
            while True:
                # Wait for the next audio block from the callback
                data = q.get()
                if rec.AcceptWaveform(data):
                    # When Vosk has a final result, parse it
                    result = json.loads(rec.Result())
                    text = result.get("text", "")
                    if not text:
                        continue
                    print("Recognized:", text)
                    token = parse_command(text)
                    if not token:
                        print("No matching command for:", text)
                        continue

                    # For light ON commands, request the current light level first
                    if token in ("BATH_ON", "HALL_ON"):
                        if transport.ser or transport.ble:
                            status_reply, light_val = transport.request_status_and_parse_light()
                            if light_val is None:
                                # If we couldn't read the light level, fall back to sending the command
                                print("Could not read light level; sending command anyway.")
                                reply = transport.send_token_and_wait_ack(token)
                                print("Device reply:", reply)
                            else:
                                print("Current light level:", light_val)
                                if light_val > LIGHT_THRESHOLD:
                                    # If the room is bright, skip turning lights on
                                    print("Room is bright (>{}). Skipping {}.".format(LIGHT_THRESHOLD, token))
                                    continue
                                else:
                                    reply = transport.send_token_and_wait_ack(token)
                                    print("Device reply:", reply)
                        else:
                            print("No transport available. Token:", token)
                    else:
                        # Non-light-on tokens are sent directly
                        if transport.ser or transport.ble:
                            reply = transport.send_token_and_wait_ack(token)
                            print("Device reply:", reply)
                        else:
                            print("No transport available. Token:", token)
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        # Ensure transports are stopped cleanly on exit
        transport.stop()

if __name__ == "__main__":
    main()

