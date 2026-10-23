#!/usr/bin/env python3
"""Talk to the buddy over USB serial.

  buddy_serial.py log [seconds]          print the serial log
  buddy_serial.py send "STATUS"          send a command, then print the log for a few seconds
  buddy_serial.py wifi                   prompt for WiFi SSID/password and provision the buddy
"""
import getpass
import glob
import sys
import time

import serial


def port():
    ports = [p for p in glob.glob("/dev/cu.usbmodem*") if "SN" not in p]
    if not ports:
        sys.exit("buddy not found on USB")
    return ports[0]


def open_port():
    s = serial.Serial()
    s.port = port()
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False  # don't reset the board when opening
    s.rts = False
    s.open()
    return s


def log(s, seconds):
    end = time.time() + seconds
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "log"
    s = open_port()
    if cmd == "log":
        log(s, float(sys.argv[2]) if len(sys.argv) > 2 else 10)
    elif cmd == "send":
        s.write((sys.argv[2] + "\n").encode())
        log(s, float(sys.argv[3]) if len(sys.argv) > 3 else 3)
    elif cmd == "wifi":
        ssid = input("WiFi network (SSID): ").strip()
        pw = getpass.getpass("Password: ")
        if "\t" in ssid or "\n" in ssid + pw:
            sys.exit("tabs/newlines are not supported")
        s.write(f"WIFI {ssid}\t{pw}\n".encode())
        print("sent; buddy is rebooting...")
        s.close()
        time.sleep(4)
        s = open_port()
        log(s, 20)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
