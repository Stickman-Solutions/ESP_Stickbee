#pragma once

#include <stdbool.h>


bool init_sensors(void);
bool deinit_sensors(void);
bool read_temperature_and_humidity(float *temperature_out, float *humidity_out);
bool read_illuminance(float *illuminance_out);
bool read_battery_voltage(int *battery_mv_out);

void bsp_turnon_identify_led(void);
void bsp_turnon_network_led(void);
void bsp_turnoff_identify_led(void);
void bsp_turnoff_network_led(void);
void bsp_turnon_pir_led(void);
void bsp_turnoff_pir_led(void);
bool bsp_motion_detected(void);
bool bsp_button_pressed(void);
void bsp_pir_handle_isr(bool occupied);
