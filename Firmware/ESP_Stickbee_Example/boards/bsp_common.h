#pragma once

#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


bool bsp_init_sensors(void);
bool bsp_deinit_sensors(void);
bool bsp_read_temperature_and_humidity(float *temperature_out, float *humidity_out);
bool bsp_read_illuminance(float *illuminance_out);
bool bsp_read_battery_voltage(int *battery_mv_out);

void bsp_turnon_identify_led(void);
void bsp_turnon_network_led(void);
void bsp_turnoff_identify_led(void);
void bsp_turnoff_network_led(void);
void bsp_turnon_pir_led(void);
void bsp_turnoff_pir_led(void);
bool bsp_motion_detected(void);
bool bsp_button_pressed(void);
void bsp_button_handle_isr(bool pressed);
void bsp_pir_handle_isr(bool occupied);
void bsp_gpio_init(void);
void bsp_gpio_start_isr(TaskHandle_t pir_task, TaskHandle_t button_task);
void bsp_deinit_for_restart(void);
