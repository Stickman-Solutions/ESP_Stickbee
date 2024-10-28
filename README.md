# Overview
This repository serves as documentation for the open source ESP_Stickbee project. The ESP_Stickbee is a Zigbee-compatible smart home development board designed by Stickman Solutions LLC that sports the following features:
- Powered by Espressif ESP32C6
- Motion sensing via BS612 PIR sensor
- Temperature and humidity sensing with TI HDC1080
- Illuminance sensing with TI OPT3001
- Low-power: ~140uA idle draw from AA battery in light sleep. Battery life is still in testing, but estimated 2-3 months on single AA using example firmware
- Open-source hardware and software examples for integrating with Home Assistant!
- Expandable GPIO header for additional sensors
The goal of the ESP_Stickbee is not to be a finished smart home product, but instead to serve as a development platform for hobbyists and developers to create their own Zigbee devices. Whether it be on the ESP_Stickbee hardware or not, I hope this project enables more people to create their own smart home devices.

## Firmware
Note, the ESP_Stickbee is shipped as a development board without preloaded firmware, and the firmware loaded by the end user is the responsibility of the end user. In this repo, example firmware for using the ESP_Stickbee with Zigbee is provided. The end user is free to modify the provided example as they see fit. The goal of the Zigbee example is to provide a good starting point for developers designing ESP32C6 Zigbee devices and is not to be used as production firmware. In my opinion, open-source Zigbee hardware and firmware is not common or accessible, so this is an attempt at lowering the barrier for hobbyists and developers to create Zigbee devices from the ground-up.

Additionally, example firmware that solely tests the ESP_Stickbee hardware for debugging purposes will be provided in the future.

## Hardware
In the hardware folder of this repo, a schematic, component placement file, and 3D model of the hardware is provided. I contemplated including the layer files from KiCad to allow people to order the PCBs themselves, but the design includes some controlled-impedance traces designed for specific stackups, so I believe it will be less confusing to not include these. Most people only care about the schematic, anyways.

Additionally, GPIO15, GPIO18, and GPIO19 are broken out in a header at the bottom of the board. This is a great location to add extra sensors, such as air quality, sound, or anything you can think of. Note, the example firmware does include a custom Zigbee cluster with custom attributes, so get creative!

## Integration
In the integration folder, a .js file is provided to serve as an example external converter for Zigbee2MQTT.

# Tutorials
Here are a couple brief tutorials for flashing the ESP_Stickbee with example firmware and integrating the device into Home Assistant. Hopefully soon I will have some videos detailing this process.

## Flashing firmware
First, download three .bin files from build directory in firmware folder: bootloader.bin, partition-table.bin, ESP_Stickbee.bin.

Using [Espressif's online flash tool](https://espressif.github.io/esptool-js/), flash the files in the following orders/addresses:

- 0x0:     [bootloader.bin](https://github.com/Stickman-Solutions/ESP_Stickbee/blob/main/Firmware/ESP_Stickbee_Example/build/bootloader/bootloader.bin)
- 0x8000:  [partition-table.bin](https://github.com/Stickman-Solutions/ESP_Stickbee/blob/main/Firmware/ESP_Stickbee_Example/build/partition_table/partition-table.bin)
- 0x10000: [ESP_Stickbee.bin](https://github.com/Stickman-Solutions/ESP_Stickbee/blob/main/Firmware/ESP_Stickbee_Example/build/ESP_Stickbee.bin)

## Home Assistant + Zigbee2MQTT integration
To use the example firmware with Zigbee2MQTT and Home Assistant, copy and paste the ESP_Stickbee.js file from the Integration folder into your "zigbee2mqtt" directory in Home Assistant. Then, list ESP_Stickbee.js under external_converters in configuration.yaml in the same directory. Now, a device running the example firmware should automatically join the Zigbee network, interview, and configure itself.

# Disclaimer
The provided firmware and ESP_Stickbee hardware are not certified by the FCC or any other regulatory body and the hardware is considered an exempted subassembly as defined in [47 CFR 15.101(e)](https://www.ecfr.gov/current/title-47/part-15/section-15.101#p-15.101(e)). This project is intended to be used for development and evaluation purposes only and is not certified for consumer use without further certification of the end user. The end user of the ESP_Stickbee hardware or provided example firmware is responsible for complying with their local laws and regulatory bodies. Stickman Solutions LLC is not responsible for any damage, injury, or fines resulting from the use of the ESP_Stickbee hardware or provided example firmware. Use of the ESP_Stickbee hardware and provided example firmware is at your own risk.

# Support
For any questions or issues with the project, please raise an issue [here](https://github.com/Stickman-Solutions/ESP_Stickbee/issues).