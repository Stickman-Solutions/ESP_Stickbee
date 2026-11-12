#include <bsp_common.h>
#include <esp_stickbee_rev1-0.h>
#include <driver/gpio.h>
#include <esp_sleep.h>


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


// GPIO related abstractions
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

bool bsp_motion_detected(void) {
    return gpio_get_level(PIR_GPIO);
}

bool bsp_button_pressed(void) {
    return gpio_get_level(BUTT0_GPIO) == 0;
}

void bsp_pir_handle_isr(bool occupied) {
    // Invert interrupt level
    gpio_int_type_t next_trigger = occupied ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
    ESP_ERROR_CHECK(gpio_set_intr_type(PIR_GPIO, next_trigger));

    // Change wakeup level on GPIO (also changes the button wakeup level, but this isn't as critical as PIR)
    // If looking for LOW wakeup, add BUTT0 to wakeup, if HIGH, don't include BUTT0
    esp_sleep_ext1_wakeup_mode_t next_wake_mode = occupied ? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH;
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup((1ULL << PIR_GPIO) | ((occupied? 1ULL : 0) << BUTT0_GPIO), next_wake_mode));
}