#include <bsp_common.h>
#include <esp_stickbee_rev1-0.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_err.h>



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


// Also set up GPIO interrupts here
static TaskHandle_t pir_task_handle;
static TaskHandle_t button_task_handle;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    switch (gpio_num) {
        case PIR_GPIO:
            // Disable interrupt (will be changed to occur on opposite logic level in task)
            gpio_set_intr_type(PIR_GPIO, GPIO_INTR_DISABLE);
            xHigherPriorityTaskWoken = xTaskResumeFromISR(pir_task_handle);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            break;

        case BUTT0_GPIO:
            // Disable interrupt (will be changed to occur on opposite logic level in task)
            gpio_set_intr_type(BUTT0_GPIO, GPIO_INTR_DISABLE);
            xHigherPriorityTaskWoken = xTaskResumeFromISR(button_task_handle);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            break;
    }

}

void bsp_gpio_init(TaskHandle_t pir_task, TaskHandle_t button_task) {
    pir_task_handle = pir_task;
    button_task_handle = button_task;

    gpio_config_t pir_io_conf = {
        .pin_bit_mask = (1ULL << PIR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    }; 
    gpio_config_t led0_io_conf = {
        .pin_bit_mask = (1ULL << LED0_GPIO),
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t led1_io_conf = {
        .pin_bit_mask = (1ULL << LED1_GPIO),
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t ledp_io_conf = {
        .pin_bit_mask = (1ULL << LEDP_GPIO),
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t bat_adc_en_io_conf = {
        .pin_bit_mask = (1ULL << BAT_ADC_EN_GPIO),
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t butt0_io_conf = {
        .pin_bit_mask = (1ULL << BUTT0_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,   
    };

    ESP_ERROR_CHECK(gpio_config(&pir_io_conf));
    ESP_ERROR_CHECK(gpio_config(&led0_io_conf));
    ESP_ERROR_CHECK(gpio_config(&led1_io_conf));
    ESP_ERROR_CHECK(gpio_config(&ledp_io_conf));
    ESP_ERROR_CHECK(gpio_config(&bat_adc_en_io_conf));
    ESP_ERROR_CHECK(gpio_config(&butt0_io_conf));

    // Do this to avoid race conditions on boot from PIR GPIO
    uint8_t pir_level = gpio_get_level(PIR_GPIO);
    ESP_ERROR_CHECK(gpio_set_intr_type(PIR_GPIO, pir_level ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIR_GPIO, gpio_isr_handler, (void*) PIR_GPIO));
    
    // Do this to avoid race conditions on boot from BUTT0 GPIO
    ESP_ERROR_CHECK(gpio_set_intr_type(BUTT0_GPIO, gpio_get_level(BUTT0_GPIO) ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTT0_GPIO, gpio_isr_handler, (void*) BUTT0_GPIO));  

    // If looking for LOW wakeup, add BUTT0 to wakeup, if HIGH, don't include BUTT0
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup((1ULL << PIR_GPIO) | ((pir_level ? 1ULL : 0) << BUTT0_GPIO), pir_level ? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH));
}