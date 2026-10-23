// ===================== USB: serial console + Mac wake-up keyboard =====================
// The buddy runs the native USB stack (TinyUSB) itself so it can show up as a
// serial port and a keyboard at once. The Mac suspends the USB bus while it
// sleeps: the buddy then turns its screen off, and POST /api/wake (or the WAKE
// serial command) sends a remote-wakeup signal to bring the Mac back. While the
// Mac is awake, a wake request taps Shift and nudges the mouse a pixel and back:
// with the display off (or in a DarkWake) macOS ignores the Shift tap alone.
// The bus stays up while only the Mac's display is off, so tools/mac-display on
// the Mac reports that over POST /api/display; the screen goes dark then too.

USBCDC USBSerial(0);
USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;

std::atomic<bool> usbSuspended{false};
std::atomic<bool> usbMounted{false};
std::atomic<bool> usbWakeAllowed{false};  // the host enabled remote wakeup before suspending
uint32_t lastWakeAt = 0;

static void onUsbEvent(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base != ARDUINO_USB_EVENTS) return;
  auto *ev = (arduino_usb_event_data_t *)data;
  switch (id) {
    case ARDUINO_USB_STARTED_EVENT: usbMounted = true; usbSuspended = false; break;
    case ARDUINO_USB_STOPPED_EVENT: usbMounted = false; usbSuspended = false; break;
    case ARDUINO_USB_SUSPEND_EVENT:
      usbWakeAllowed = ev->suspend.remote_wakeup_en;
      usbSuspended = true;
      break;
    case ARDUINO_USB_RESUME_EVENT: usbSuspended = false; break;
  }
}

// Called first thing in setup(), before anything prints to Serial
void usbBegin() {
  USB.productName("Desk Buddy");
  USB.usbAttributes(TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP);
  USB.onEvent(onUsbEvent);
  Keyboard.begin();
  Mouse.begin();
  USBSerial.begin(115200);
  USB.begin();
}

// The Mac is asleep: USB enumerated earlier and the bus is now suspended.
// Unplugged or on a charger (never mounted) counts as awake.
bool macAsleep() { return usbMounted && usbSuspended; }

// Display state from tools/mac-display (heartbeat every minute). Stale reports
// are ignored, so a buddy moved away from that Mac lights up again.
static constexpr uint32_t DISPLAY_REPORT_TTL_MS = 5 * 60000;
bool macDisplayOn = true;
uint32_t macDisplayAt = 0;

void setMacDisplay(bool on) {
  macDisplayOn = on;
  macDisplayAt = millis();
  // A report means the Mac is running. The ESP32-S3 sometimes misses the USB
  // resume after a wake, which left the screen off with the Mac awake.
  usbSuspended = false;
}

bool macDisplayOff() { return macDisplayAt && !macDisplayOn && millis() - macDisplayAt < DISPLAY_REPORT_TTL_MS; }

// The buddy's screen follows the Mac: off while it sleeps or its display is off
bool macDark() { return macAsleep() || macDisplayOff(); }

const char *usbStateName() {
  if (!usbMounted) return "unplugged";
  return usbSuspended ? "suspended" : "active";
}

// Returns what it did, for the HTTP/serial reply
const char *wakeMac() {
  lastWakeAt = millis();
  if (!usbMounted) return "not connected to a computer";
  if (usbSuspended) {
    if (!usbWakeAllowed) return "host did not enable remote wakeup";
    return tud_remote_wakeup() ? "remote wakeup sent" : "remote wakeup failed";
  }
  Keyboard.press(KEY_LEFT_SHIFT);
  delay(30);
  Keyboard.releaseAll();
  Mouse.move(1, 0);
  delay(30);
  Mouse.move(-1, 0);
  return "shift tapped, mouse nudged";
}
