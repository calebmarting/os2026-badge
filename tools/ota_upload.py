#!/usr/bin/env python3
"""Update every badge on the guitar neck through the master's USB port.

The master (badge ID 0) receives firmware.bin over USB serial, then sends it to
all the fret badges over I2C and reboots everything into it. See src/ota.h.

    python tools/ota_upload.py .pio/build/mkrzero/firmware.bin
    python tools/ota_upload.py --port COM7 firmware.bin

Or let PlatformIO build first and find the port:  pio run -t ota

Needs pyserial, which PlatformIO's own Python already has.
"""

import argparse
import sys
import time
import zlib

import serial
import serial.tools.list_ports

ARDUINO_VID = 0x2341
CHUNK = 256          # must match NVM_ROW_SIZE: the master ACKs one row at a time
APP_MAX_SIZE = 0x1E000


def find_port():
    ports = [p.device for p in serial.tools.list_ports.comports() if p.vid == ARDUINO_VID]
    if len(ports) == 1:
        return ports[0]
    if not ports:
        sys.exit("No badge found on USB. Plug in the master, or pass --port.")
    sys.exit("Several badges on USB (%s). Pass --port for the master." % ", ".join(ports))


def wait_for(ser, prefix, timeout):
    """Echo the master's output until a line starting with prefix arrives."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = ser.readline().decode("ascii", "replace").strip()
        if not line:
            continue
        if line.startswith("OTA FAIL"):
            sys.exit(line)
        if line.startswith(prefix):
            return line
        if line.startswith("OTA"):
            print(line)
    sys.exit("Timed out waiting for '%s' from the master." % prefix)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", help="firmware.bin to send")
    parser.add_argument("--port", help="the master's serial port (default: auto-detect)")
    args = parser.parse_args()

    data = open(args.image, "rb").read()
    if len(data) > APP_MAX_SIZE:
        sys.exit("%s is %d bytes; I2C updates allow at most %d." % (args.image, len(data), APP_MAX_SIZE))
    crc = zlib.crc32(data) & 0xFFFFFFFF
    port = args.port or find_port()
    print("Sending %s (%d bytes, CRC %08X) to the master on %s" % (args.image, len(data), crc, port))

    with serial.Serial(port, 115200, timeout=0.2) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.write(b"\nota %d %d\n" % (len(data), crc))
        wait_for(ser, "OTA READY", 15)

        for offset in range(0, len(data), CHUNK):
            ser.write(data[offset:offset + CHUNK])
            wait_for(ser, "OTA ACK", 10)
            done = min(len(data), offset + CHUNK)
            print("\rTo master: %3d%%" % (done * 100 // len(data)), end="", flush=True)
        print()

        wait_for(ser, "OTA STAGED", 15)
        print("Master has the image; updating the neck over I2C...")
        wait_for(ser, "OTA DONE", 180)
        print("Done. Every badge is rebooting into the new firmware;")
        print("run the master's 'info' command to check their firmware CRCs match.")


if __name__ == "__main__":
    main()
