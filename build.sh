#!/usr/bin/sh
# Just a helper script for building and uploading when multiple ttyUSB devices exist.
dev="/dev/serial/by-id/usb-1a86_USB2.0-Serial-if00-port0"
if [ "$1" = "mon" ]
then
    pio device monitor --port $dev
else
    pio run --upload-port $dev --monitor-port $dev
fi
