# Desk Buddy

An animated desk companion on a Seeed XIAO ESP32-S3 Plus with the
1.47" touch display (JD9853A 172×320, AXS5106L touch, LSM6DS3 IMU).

```
firmware/buddy/   Arduino sketch
vendor/           Seeed_GFX2 (display library) and Seeed's Display-Gadgets, as git submodules
vendor-patches/   a speed patch for Seeed_GFX2
case/             CadQuery case generator + STL/STEP output in case/out/
tools/            buddy_serial.py: USB log / commands / WiFi provisioning; mac-display
```

It pairs with [homelab-agent](https://github.com/sjespersen/homelab-agent): its agent can wake
your Mac through the buddy (`WAKE_TARGETS="mac=http://buddy.local/api/wake"`, see
[Mac wake-up](#mac-wake-up)).

## Pages

The pages cycle on their own (face 30 s, weather 15 s, clock 15 s; `PAGE_SHOW_MS` in
`buddy.h`). Swiping or the two buttons under the screen switch pages by hand, and any
touch, button press or tilt pauses the cycling for a minute.

| Page | What it does |
|---|---|
| Face | Eyes follow your finger, blink and look around. Tap: boop. Poke an eye: it squints. Stroke it: hearts. Tap 5× fast: grumpy. Shake: dizzy. Tilt: it looks downhill. Gets sleepy from 22:00 and sleeps (screen dimmed) from 23:00 to 07:00 (`SLEEPY_HOUR`/`SLEEP_HOUR`/`WAKE_HOUR` in `buddy.h`). |
| Weather | Current conditions for your location (icon, temperature, high/low, feels-like, rain chance, wind). The bottom strip shows the next 12 hours (temperature curve, rain-chance bars); tap to switch to the next 3 days. |
| Clock | Time, date, a seconds sweep and a one-line weather summary. |

## Weather and time

The buddy fetches the forecast itself from [Open-Meteo](https://open-meteo.com) every
15 minutes over HTTPS. By default it finds its location by
looking up its public IP at ipinfo.io, which sends that IP to ipinfo.io and the rough
coordinates to Open-Meteo. To pin the location instead (it's saved on the buddy):

```sh
curl -d set=52.52,13.40,Berlin http://buddy.local/api/location   # set= alone: back to IP lookup
```

or `LOC 52.52,13.40,Berlin` over serial. The time comes from NTP, and the clock uses
the UTC offset Open-Meteo reports for the location (Europe/Berlin until the first
forecast arrives).

## Build & flash

Seeed_GFX2 is a submodule at a tested commit, and needs a small speed patch (bulk SPI pixel
writes, direct sprite line fills; without it the UI runs at ~3 fps):

```sh
git submodule update --init vendor/Seeed_GFX2
git -C vendor/Seeed_GFX2 apply ../../vendor-patches/seeed_gfx2-perf.patch
```

```sh
FQBN="esp32:esp32:XIAO_ESP32S3_Plus:PSRAM=opi,USBMode=default,CDCOnBoot=cdc"   # TinyUSB, see "Mac wake-up"
arduino-cli compile -b $FQBN --library vendor/Seeed_GFX2 --build-path build firmware/buddy
arduino-cli upload  -b $FQBN -p <port> --input-dir build firmware/buddy   # <port>: e.g. /dev/cu.usbmodem…
```

To be able to go back to Seeed's factory demo, dump it before the first flash (`backup/` stays
out of git): `esptool --port <port> read-flash 0 ALL backup/factory_full_16MB.bin`, and later
`esptool --port <port> write-flash 0 backup/factory_full_16MB.bin`.

## Mac wake-up

Plugged into the Mac, the buddy is a serial port *and* a USB keyboard (native
TinyUSB stack, `usbwake.ino`; the first time, macOS opens the Keyboard Setup
Assistant, just close it).

- When the Mac sleeps it suspends the USB bus; the buddy then turns its screen off
  and back on when the Mac wakes.
- `curl -X POST http://buddy.local/api/wake` (or `WAKE` over serial) wakes the Mac:
  a USB remote-wakeup while it sleeps, a Shift tap plus a 1-pixel mouse nudge while it is awake
  (wakes the display; macOS ignores the Shift tap alone while the display is off or in a DarkWake).
  `/api/state` reports `usb`: `active`, `suspended` or `unplugged`.
- The bus stays up while only the Mac's *display* is off, so `tools/mac-display`
  (a small launch agent, `tools/mac-display/install.sh`) reports display sleep/wake
  to `POST /api/display on=0|1`, with a heartbeat every minute. The buddy goes dark
  then too, and ignores reports older than 5 minutes (e.g. after moving it to
  another computer). `/api/state` shows `mac_display`.
- Only works while the Mac can be woken by a keyboard: lid open, or closed with an
  external display. Anyone on the home network can call `/api/wake`; all it does is
  wake the Mac.

If an upload fails, the board is often already in its bootloader and shows up under another
port (e.g. `/dev/cu.usbmodem1101`): upload again to that port. If the firmware hangs, hold
BOOT while plugging in and flash again.

## WiFi

With no saved network the buddy opens the `Buddy-Setup` hotspot; join it and open
192.168.4.1. Or over USB: `tools/buddy_serial.py wifi`. Hold button B for 4 s to forget
the network. The buddy is then at `http://buddy.local` (`/api/state` for debug).

Serial commands: `STATUS`, `ROT 1|3` (flip the screen 180°), `RESETWIFI`, `LOC` (pin the
weather location), `PERF` (frame timing + touch gesture log), `BENCH` (time the drawing
routines). Note that opening the
serial port reboots the board. `http://buddy.local/api/state` also reports the frame rate.

## Security

The buddy's HTTP API has no authentication: anyone on your LAN can change its weather
location. Keep it on a trusted network.

## Case

```sh
git submodule update --init --checkout vendor/Display-Gadgets   # Seeed's board model, ~1 GB; only for --check
python3 -m venv .venv-cad && .venv-cad/bin/pip install cadquery matplotlib
.venv-cad/bin/python case/buddy_case.py --check "vendor/Display-Gadgets/resources/3d-model/XIAO 1.47'' IPS Display.step"
```

Two parts, both exported in print orientation (PLA/PETG, 0.2 mm, no supports):

- `front_shell.stl`: print face down. Screen window, flex-tongue buttons, mic hole,
  openings for USB-C (left), microSD (right) and the power switch (top).
  A slot in the top wall (next to the switch) lets the antenna out; stick the FPC on
  the outside top or let it stand up.
- `back_cover.stl`: prints standing on its wedge foot. Snap-fit lip, four posts whose
  pins sit in the PCB's mounting holes and hold the board down. The wedge makes the
  buddy sit on the table leaning back 45°.

Tune `LID_FIT` (lid tightness), `SNAP` (snap strength), `BACK_SPACE` (depth behind the
board), `TILT_DEG` and `FOOT_LEN` (None = full-size wedge) at the top of the script.
The `--check` run intersects the parts with Seeed's board model and reports collisions.

## License

MIT (see `LICENSE`). Seeed_GFX2 and Display-Gadgets keep their own licenses.
