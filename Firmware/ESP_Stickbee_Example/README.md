# Description:
Open source firmware for ESP32-C6 Zigbee device.

# Container setup
These instructions are written assuming a Fedora host using Podman. These instructions can probably be extended to Docker and other hosts, but have not been tested by me. The only prerequisite is allowing unprivileged containers to access /dev to be able to flash the firmware. To do this permanently, do `sudo setsebool -P container_use_devices on`. Make sure you understand the implications of that command before running it.

## Building app (without USB device connected)
First start the container with `podman-compose run --rm idf`\
Inside the container, run `idf.py build`\

## Flashing to board (USB device connected)
The compose file assumes the device is mapped to `/dev/ttyACM0`. If this is not the case on your machine, make sure to edit the compose file.

First, start the container with `podman-compose run --rm flash`\
Inside the container, run `flash`. You may also want to run `idf.py build` or `idf.py erase-flash` prior to this.

If monitoring with `idf.py monitor`, I've seen it get stuck when trying to reconnect. In this case, press `CTRL+]` and restart the container.

# Programming instructions:
First, download three .bin files from build directory: bootloader.bin, partition-table.bin, ESP_Stickbee.bin.

Using https://espressif.github.io/esptool-js/, flash the files in the following orders/addresses:

0x0:     bootloader.bin
0x8000:  partition-table.bin
0x10000: ESP_Stickbee.bin
