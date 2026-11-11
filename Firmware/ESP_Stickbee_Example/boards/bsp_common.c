#include <bsp_common.h>
#include <esp_stickbee_rev1-0.h>
#include <driver/gpio.h>


bool init_sensors(void) {
    return init_i2c_sensors();
}


bool deinit_sensors(void) {
    return deinit_i2c_sensors();
}


bool read_temperature_and_humidity(float *temperature_out, float *humidity_out) {
    return hdc1080_read_temperature_and_humidity(temperature_out, humidity_out);
}


bool read_illuminance(float *illuminance_out) {
    return opt3001_read_illuminance(illuminance_out);
}


bool read_battery_voltage(int *battery_mv_out) {
    return adc_read_battery_voltage(battery_mv_out);
}


static void hold_gpio_level(gpio_num_t gpio, bool level) {
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_hold_dis(gpio);
    gpio_set_level(gpio, level);
    gpio_hold_en(gpio);
}

static void disable_gpio(gpio_num_t gpio) {
    gpio_hold_dis(gpio);
    gpio_set_direction(gpio, GPIO_MODE_DISABLE);
}

void bsp_turnon_identify_led(void) {
    hold_gpio_level(IDENTIFY_LED_GPIO, 0);
}

void bsp_turnoff_identify_led(void) {
    disable_gpio(IDENTIFY_LED_GPIO);
}

void bsp_turnon_network_led(void) {
    hold_gpio_level(NETWORK_LED_GPIO, 0);
}

void bsp_turnoff_network_led(void) {
    disable_gpio(NETWORK_LED_GPIO);
}

void bsp_turnon_pir_led(void) {
    hold_gpio_level(LEDP_GPIO, 0);
}

void bsp_turnoff_pir_led(void) {
    disable_gpio(LEDP_GPIO);
}
