#pragma once

#include <esp_stickbee_rev1-0.h>


bool init_sensors();
bool read_temperature_and_humidity(float *temperature_out, float *humidity_out);
bool read_illuminance(float *illuminance_out);
bool read_battery_voltage(float *battery_voltage_out);