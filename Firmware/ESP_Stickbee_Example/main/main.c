#include "zigbee_funcs.h"
#include "periph_funcs.h"

#include "bsp_common.h"

#include "esp_pm.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "esp_private/esp_clk.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "math.h"

///////////////////////////////////////////////////////////////////////////////////////////
// Constant definitions (including pin numbers)
///////////////////////////////////////////////////////////////////////////////////////////
#define MAJOR_SOFTWARE_VER 0
#define MINOR_SOFTWARE_VER 1

#define SENSOR_UPDATE_PERIOD_SEC 300
#define ZIGBEE_SLEEP_TIME_MS 9000

#define ESP_INTR_FLAG_DEFAULT 0
#define LONG_PRESS_TIME_MS 1000
#define LED_TURNOFF_TIMEOUT_MS 30000
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// ADC variables
///////////////////////////////////////////////////////////////////////////////////////////
adc_oneshot_unit_handle_t bat_adc_handle;
adc_oneshot_unit_init_cfg_t bat_adc_init_config = {
    .unit_id = BAT_ADC,
};
adc_oneshot_chan_cfg_t bat_adc_config = {
    .bitwidth = ADC_BITWIDTH_DEFAULT,
    .atten = BAT_ADC_ATTEN,
};
adc_cali_handle_t bat_adc_cali_handle = NULL;
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Zigbee variables
///////////////////////////////////////////////////////////////////////////////////////////
uint8_t first_sensor_sample_flag = 0;   // Flag is set after first sensor sample occurs (stops Zigbee from reporting 0s on restart)
uint8_t hdc1080_done_flag = 0;          // Set after reading callback has been called
extern uint8_t leds_enabled;
extern int16_t zb_bat_voltage, zb_temperature;
extern uint16_t zb_humidity, zb_illuminance;
extern uint8_t zb_started_flag;
uint8_t long_press_alarm_scheduled = 0;
uint8_t led_turnoff_alarm_scheduled = 0;

uint8_t PIR_interrupt_type, BUTT0_interrupt_type;

///////////////////////////////////////////////////////////////////////////////////////////

static const char *TAG = "ESP_STICKBEE";

///////////////////////////////////////////////////////////////////////////////////////////
// Update PIR sensor attribute when interrupt is called
///////////////////////////////////////////////////////////////////////////////////////////
TaskHandle_t pir_sensor_update_handle = NULL;
void pir_sensor_update(void *arg) {
    //uint32_t io_num;
    uint8_t zb_occupancy;
    while (1) {
        zb_occupancy = gpio_get_level(PIR_GPIO);
        PIR_interrupt_type = zb_occupancy? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;

        // Change interrupt level
        ESP_ERROR_CHECK(gpio_set_intr_type(PIR_GPIO, PIR_interrupt_type));
        // Change wakeup level on GPIO (also changes the button wakeup level, but this isn't as critical as PIR)
        // If looking for LOW wakeup, add BUTT0 to wakeup, if HIGH, don't include BUTT0
        ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup((1ULL << PIR_GPIO) | ((zb_occupancy? 1ULL : 0) << BUTT0_GPIO), zb_occupancy? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH));

        // If Zigbee has started, update the attribute
        if (zb_started_flag == 1) {
            esp_zb_lock_acquire(portMAX_DELAY);
            esp_zb_zcl_set_attribute_val(HA_ESP_SENSOR_ENDPOINT,
                ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID, &zb_occupancy, false);
            esp_zb_lock_release();
        }
        ESP_LOGI(TAG, "PIR sensor: %d", zb_occupancy);
        vTaskSuspend(NULL); // Suspend task until resumed by ISR
    }
}
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Timer callback for LED turnoff
///////////////////////////////////////////////////////////////////////////////////////////
static void led_turnoff_timer_callback(uint8_t input) {
    ESP_LOGI(TAG, "LED turnoff timer callback");
    led_turnoff_alarm_scheduled = 0;
    // Turn off LEDs
    leds_enabled = 0;
    gpio_hold_dis(IDENTIFY_LED_GPIO);
    gpio_set_direction(IDENTIFY_LED_GPIO, GPIO_MODE_DISABLE);
    gpio_hold_dis(NETWORK_LED_GPIO);
    gpio_set_direction(NETWORK_LED_GPIO, GPIO_MODE_DISABLE);
}
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Timer callback for button long press
///////////////////////////////////////////////////////////////////////////////////////////
static void long_press_timer_callback(uint8_t input) {
    ESP_LOGI(TAG, "Long press timer callback");
    long_press_alarm_scheduled = 0;
    // If button is still pressed
    if (gpio_get_level(BUTT0_GPIO) == 0) {
        //i2c_master_bus_rm_device(hdc1080_dev_handle);  // Should be redundant if deleting the bus
        //i2c_master_bus_rm_device(opt3001_dev_handle);  // Should be redundant if deleting the bus
        //i2c_del_master_bus(i2c_bus_handle);
        gpio_uninstall_isr_service();
        gpio_hold_dis(LED0_GPIO);
        gpio_hold_dis(LED1_GPIO);
        gpio_hold_dis(LEDP_GPIO);
        ESP_LOGI(TAG, "Factory resetting");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_zb_factory_reset();
    }
}
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Handle button press/release
///////////////////////////////////////////////////////////////////////////////////////////
TaskHandle_t button_handler_handle = NULL;
void button_handler(void *arg) {
    uint8_t button_level;
    while(1) {
        button_level = gpio_get_level(BUTT0_GPIO);
        BUTT0_interrupt_type = button_level? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;

        // Wait for Zigbee stack to be started
        if (zb_started_flag) {
            // Button pressed
            if (button_level == 0) {
                // Cancel current alarm
                if (long_press_alarm_scheduled) {
                    esp_zb_scheduler_alarm_cancel((esp_zb_callback_t)long_press_timer_callback, 0);
                    long_press_alarm_scheduled = 0;
                }
                // Begin recording for long press
                esp_zb_scheduler_alarm((esp_zb_callback_t)long_press_timer_callback, 0, LONG_PRESS_TIME_MS);
                long_press_alarm_scheduled = 1;

                if (leds_enabled == 0) {
                    // Turn on identify LED
                    gpio_set_direction(IDENTIFY_LED_GPIO, GPIO_MODE_OUTPUT);
                    gpio_hold_dis(IDENTIFY_LED_GPIO);
                    gpio_set_level(IDENTIFY_LED_GPIO, 0);
                    gpio_hold_en(IDENTIFY_LED_GPIO);
                    // Set network LED
                    if (esp_zb_bdb_dev_joined()) {
                        gpio_set_direction(NETWORK_LED_GPIO, GPIO_MODE_OUTPUT);
                        gpio_hold_dis(NETWORK_LED_GPIO);
                        gpio_set_level(NETWORK_LED_GPIO, 0);
                        gpio_hold_en(NETWORK_LED_GPIO);
                    }
                    leds_enabled = 1;

                    // Cancel current alarm
                    if (led_turnoff_alarm_scheduled) {
                        esp_zb_lock_acquire(portMAX_DELAY);
                        esp_zb_scheduler_alarm_cancel((esp_zb_callback_t)led_turnoff_timer_callback, 0);
                        esp_zb_lock_release();
                        led_turnoff_alarm_scheduled = 0;
                    }
                    // Begin timer to turn off LEDs
                    esp_zb_lock_acquire(portMAX_DELAY);
                    esp_zb_scheduler_alarm((esp_zb_callback_t)led_turnoff_timer_callback, 0, LED_TURNOFF_TIMEOUT_MS);
                    esp_zb_lock_release();
                    led_turnoff_alarm_scheduled = 1;
                    
                }
                else {
                    // Turn off LEDs
                    leds_enabled = 0;
                    gpio_hold_dis(IDENTIFY_LED_GPIO);
                    gpio_set_direction(IDENTIFY_LED_GPIO, GPIO_MODE_DISABLE);
                    gpio_hold_dis(NETWORK_LED_GPIO);
                    gpio_set_direction(NETWORK_LED_GPIO, GPIO_MODE_DISABLE);

                    // Cancel current alarm
                    if (led_turnoff_alarm_scheduled) {
                        esp_zb_lock_acquire(portMAX_DELAY);
                        esp_zb_scheduler_alarm_cancel((esp_zb_callback_t)led_turnoff_timer_callback, 0);
                        esp_zb_lock_release();
                        led_turnoff_alarm_scheduled = 0;
                    }

                }
                
            }
        }

        // Change interrupt level
        ESP_ERROR_CHECK(gpio_set_intr_type(BUTT0_GPIO, BUTT0_interrupt_type));
        // Change wakeup level on GPIO
        //ESP_ERROR_CHECK(gpio_wakeup_disable(BUTT0_GPIO));
        //ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO));
        //ESP_ERROR_CHECK(gpio_wakeup_enable(BUTT0_GPIO, BUTT0_interrupt_type));   
        //ESP_ERROR_CHECK(gpio_wakeup_enable(PIR_GPIO, PIR_interrupt_type));  // Make sure PIR wakeup stays enabled
        //ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

        ESP_LOGI(TAG, "Button %s", button_level? "released" : "pressed");
        vTaskSuspend(NULL);
    }
}
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Set up PIR sensor interrupt
///////////////////////////////////////////////////////////////////////////////////////////
static QueueHandle_t gpio_isr_evt_queue = NULL;
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    switch (gpio_num) {
        case PIR_GPIO:
            // Disable interrupt (will be changed to occur on opposite logic level in task)
            gpio_set_intr_type(PIR_GPIO, GPIO_INTR_DISABLE);
            xHigherPriorityTaskWoken = xTaskResumeFromISR(pir_sensor_update_handle);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            break;

        case BUTT0_GPIO:
            // Disable interrupt (will be changed to occur on opposite logic level in task)
            gpio_set_intr_type(BUTT0_GPIO, GPIO_INTR_DISABLE);
            xHigherPriorityTaskWoken = xTaskResumeFromISR(button_handler_handle);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            break;
    }

}
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Configure ADC
///////////////////////////////////////////////////////////////////////////////////////////
static esp_err_t adc_init(void) {
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&bat_adc_init_config, &bat_adc_handle), TAG, "Failed to create ADC handle");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(bat_adc_handle, BAT_ADC_CH, &bat_adc_config), TAG, "Failed to config ADC channel");

    if (adc_calibration_init(BAT_ADC, BAT_ADC_CH, BAT_ADC_ATTEN, &bat_adc_cali_handle)!= true) {
        return !ESP_OK;
    }
    return ESP_OK;
}
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Function to update all sensors (HDC1080 and ADC)
///////////////////////////////////////////////////////////////////////////////////////////
static void update_all_sensors(void *arg) {
    int adc_raw, voltage;
    float temperature, humidity, lux;

    while (1) {

        ESP_LOGI(TAG, "UPDATE ALL SENSORS:");

        // Acquire lock to make sure Zigbee stuff isn't called during
        if (zb_started_flag == 1) {
            esp_zb_lock_acquire(portMAX_DELAY);
        }

        // Lock power management to stop light sleep from occurring during delay
        esp_pm_lock_handle_t pm_lock;
        esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no_light_sleep", &pm_lock);
        esp_pm_lock_acquire(pm_lock);

        init_i2c_sensors();

        // Read and update temp and humidity
        if (read_temperature_and_humidity(&temperature, &humidity)) {

            zb_temperature = (int16_t) ((temperature+0.005)*100.0);   // Add 0.005 to do rounding instead of truncating
            zb_humidity = (uint16_t) ((humidity+0.005)*100.0);         // Add 0.005 to do rounding instead of truncating

            ESP_LOGI("SENSOR_DATA", "TEMPERATURE: %.2f°C", temperature);
            ESP_LOGI("SENSOR_DATA", "HUMIDITY: %.2f%%", humidity);

            // If Zigbee stack has been started, update attributes
            if (zb_started_flag == 1) {
                esp_zb_zcl_set_attribute_val(HA_ESP_SENSOR_ENDPOINT,
                    ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, &zb_temperature, false);

                esp_zb_zcl_set_attribute_val(HA_ESP_SENSOR_ENDPOINT,
                    ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, &zb_humidity, false);
            }
        }

        // Read and update ALS
        if (read_illuminance(&lux)) {
            if (lux < 1) {  // Minimum value in ZCL definition
                zb_illuminance = 0;
            }
            else {
                zb_illuminance = (uint16_t) (10000.0 * log10f(lux)) + 1;
            }

            // If Zigbee stack has been started, update attribute
            if (zb_started_flag == 1) {
                esp_zb_zcl_set_attribute_val(HA_ESP_SENSOR_ENDPOINT,
                    ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID, &zb_illuminance, false);
            }           
        }

        deinit_i2c_sensors();

        // Enable voltage divider
        gpio_set_direction(BAT_ADC_EN_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(BAT_ADC_EN_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(ADC_DELAY_MS));

        // Try initializing and deinitializing ADC before each sample (hoping to fix constant 4081 issue)
        adc_init();

        // Read ADC
        if(adc_oneshot_read(bat_adc_handle, BAT_ADC_CH, &adc_raw) == ESP_OK) {
            ESP_LOGI("ADC", "ADC value: %d", adc_raw);
        }
        if (adc_cali_raw_to_voltage(bat_adc_cali_handle, adc_raw, &voltage) == ESP_OK) {
            ESP_LOGI("ADC", "ADC voltage: %d", voltage);
        }

        // Release power management
        esp_pm_lock_release(pm_lock);
        if (zb_started_flag == 1) {
            esp_zb_lock_release();
        }

        // Disable voltage divider pin
        gpio_set_direction(BAT_ADC_EN_GPIO, GPIO_MODE_DISABLE);

        adc_calibration_deinit(bat_adc_cali_handle);
        adc_oneshot_del_unit(bat_adc_handle);

        zb_bat_voltage = (int16_t)voltage * 2;  // Consider the voltage divider

        // If Zigbee has been started, update attribute
        if (zb_started_flag == 1) {
            esp_zb_lock_acquire(portMAX_DELAY);
            esp_zb_zcl_set_attribute_val(HA_ESP_SENSOR_ENDPOINT,
                CUSTOM_ELECTRICAL_CLUSTER_ID, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                CUSTOM_ELECTRICAL_CLUSTER_BATTERY_ATTR_ID, &zb_bat_voltage, false);
            esp_zb_lock_release();
        }
        
        // Set flag first time this is sampled
        if (first_sensor_sample_flag == 0) {
            first_sensor_sample_flag = 1;
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_UPDATE_PERIOD_SEC * 1000));
    }
}
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Configure HDC1080 and start multisense sampling loop
// DEPRECATED, I2C IS INITIALIZED AND DEINITIALIZED EVERY LOOP
///////////////////////////////////////////////////////////////////////////////////////////
static esp_err_t multisensor_init(void)
{
    return (xTaskCreate(update_all_sensors, "update_all_sensors", 8192, NULL, 10, NULL) == pdTRUE) ? ESP_OK : ESP_FAIL;
}
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Configure GPIO
///////////////////////////////////////////////////////////////////////////////////////////
static esp_err_t gpio_init(void) { 
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

    ESP_RETURN_ON_ERROR(gpio_config(&pir_io_conf), TAG, "Failed to configure PIR GPIO");
    ESP_RETURN_ON_ERROR(gpio_config(&led0_io_conf), TAG, "Failed to configure LED0 GPIO");
    ESP_RETURN_ON_ERROR(gpio_config(&led1_io_conf), TAG, "Failed to configure LED1 GPIO");
    ESP_RETURN_ON_ERROR(gpio_config(&ledp_io_conf), TAG, "Failed to configure PIR LED GPIO");
    ESP_RETURN_ON_ERROR(gpio_config(&bat_adc_en_io_conf), TAG, "Failed to configure BAT ADC enable GPIO");
    ESP_RETURN_ON_ERROR(gpio_config(&butt0_io_conf), TAG, "Failed to configure BUTT0 GPIO");

    // Do this to avoid race conditions on boot from PIR GPIO
    uint8_t pir_level = gpio_get_level(PIR_GPIO);
    PIR_interrupt_type = pir_level? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(PIR_GPIO, PIR_interrupt_type), TAG, "Failed to set PIR interrupt type");
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT), TAG, "Failed to install ISR service");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(PIR_GPIO, gpio_isr_handler, (void*) PIR_GPIO), TAG, "Failed to initialize PIR pin interrupt");
    
    // Do this to avoid race conditions on boot from BUTT0 GPIO
    BUTT0_interrupt_type = gpio_get_level(BUTT0_GPIO)? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(BUTT0_GPIO, BUTT0_interrupt_type), TAG, "Failed to set BUTT0 interrupt type");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BUTT0_GPIO, gpio_isr_handler, (void*) BUTT0_GPIO), TAG, "Failed to initialize BUTT0 pin interrupt");  

    // If looking for LOW wakeup, add BUTT0 to wakeup, if HIGH, don't include BUTT0
    ESP_RETURN_ON_ERROR(esp_sleep_enable_ext1_wakeup((1ULL << PIR_GPIO) | ((pir_level? 1ULL : 0) << BUTT0_GPIO), pir_level? ESP_EXT1_WAKEUP_ANY_LOW : ESP_EXT1_WAKEUP_ANY_HIGH), TAG, "Failed to enable EXT1 wakeup");
    gpio_isr_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    esp_err_t return_err = (xTaskCreate(button_handler, "button_handler", 4096, NULL, 10, &button_handler_handle) == pdTRUE) ? ESP_OK : ESP_FAIL;
    vTaskSuspend(button_handler_handle);  // Suspend the task initially
    return_err = (xTaskCreate(pir_sensor_update, "pir_update", 4096, NULL, 10, &pir_sensor_update_handle) == pdTRUE) ? ESP_OK : ESP_FAIL;
    vTaskSuspend(pir_sensor_update_handle);  // Suspend the task initially
    return return_err;
}
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Initialize drivers
///////////////////////////////////////////////////////////////////////////////////////////
static esp_err_t init_all_drivers(void)
{
    ESP_RETURN_ON_ERROR(gpio_init(), TAG, "Failed to set up GPIO and interrupt");
    ESP_RETURN_ON_ERROR(multisensor_init(), TAG, "Failed to initialize DHT driver");
    return ESP_OK;
}
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// App_main, really only called once. Init drivers here
///////////////////////////////////////////////////////////////////////////////////////////
void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_power_save_init());
    
    //ESP_ERROR_CHECK(esp_sleep_pd_config(ESP_PD_DOMAIN_TOP, ESP_PD_OPTION_ON));
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    // Report software version
    ESP_LOGI(TAG, "ESP_STICKBEE SOFTWARE VERSION: %d.%d", MAJOR_SOFTWARE_VER, MINOR_SOFTWARE_VER);

    // Initialize all drivers/peripherals
    ESP_LOGI(TAG, "All driver initialization %s", init_all_drivers() ? "failed" : "successful");

    // Wait for first sample to occur with a timeout
    const TickType_t timeout_ticks = pdMS_TO_TICKS(1000);  // Timeout of 10 seconds
    TickType_t start_ticks = xTaskGetTickCount();
    while (first_sensor_sample_flag == 0) {
        if (xTaskGetTickCount() - start_ticks >= timeout_ticks) {
            ESP_LOGE(TAG, "First sensor sample reading timed out");
            break;
        }
    }

    // Start Zigbee stack task
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
///////////////////////////////////////////////////////////////////////////////////////////
