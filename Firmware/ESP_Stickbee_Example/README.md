# Description:
Open source firmware for ESP32-C6 Zigbee device.

# Programming instructions:
First, download three .bin files from build directory: bootloader.bin, partition-table.bin, ESP_Stickbee.bin.

Using https://espressif.github.io/esptool-js/, flash the files in the following orders/addresses:

0x0:     bootloader.bin
0x8000:  partition-table.bin
0x10000: ESP_Stickbee.bin
