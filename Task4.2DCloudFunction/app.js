// Configuration
// Replace ENDPOINT_URL with backend or Arduino Cloud function endpoint
const ENDPOINT_URL = "http://localhost:1880/api/command";
const API_KEY = "a517604f0c112f1082111d058ed249ac";


// UI helpers
// Update the global status text shown at the top of the page
function setGlobalStatus(text) {
  const el = document.getElementById('globalStatus');
  if (el) el.textContent = text;
}

// Update the per-room status element (expects IDs like "livingStatus")
function setRoomStatus(roomId, text) {
  const el = document.getElementById(roomId + 'Status');
  if (el) el.textContent = text;
}


// Network: send command to backend
// Sends { room, action, timer } to the configured endpoint.
// Returns { ok: true, data } on success or { ok: false, error } on failure.
async function sendCommand(room, action, timerSeconds = 0) {
  const payload = { room, action, timer: Number(timerSeconds) || 0 };

  try {
    const res = await fetch(ENDPOINT_URL, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "Authorization": `Bearer ${API_KEY}`
      },
      body: JSON.stringify(payload)
    });

    if (!res.ok) {
      const text = await res.text().catch(()=>res.statusText);
      throw new Error(`HTTP ${res.status}: ${text}`);
    }

    // Try parse JSON, but tolerate empty responses
    const json = await res.json().catch(()=>null);
    return { ok: true, data: json };
  } catch (err) {
    console.error("sendCommand error", err);
    return { ok: false, error: err.message || String(err) };
  }
}


// DOM wiring: send commands when toggles change
document.addEventListener('DOMContentLoaded', () => {
  // Optional: immediate send on checkbox change (debounced)
  const debounce = {};
  document.querySelectorAll('input.toggle').forEach(cb => {
    cb.addEventListener('change', () => {
      const room = cb.dataset.room;
      clearTimeout(debounce[room]);
      debounce[room] = setTimeout(() => {
        const action = cb.checked ? "on" : "off";
        // Always send timer = 0 since timer UI removed
        sendCommand(room, action, 0).then(res => {
          if (!res.ok) {
            setRoomStatus(room.replace(/\s/g,''), 'Error');
            setGlobalStatus('Error: ' + res.error);
          } else {
            setRoomStatus(room.replace(/\s/g,''), 'Command sent');
            setGlobalStatus('Ready');
          }
        });
      }, 300);
    });
  });
});

