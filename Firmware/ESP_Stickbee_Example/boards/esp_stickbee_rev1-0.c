#include <esp_stickbee_rev1-0.h>
#include <hdc1080.h>
#include <opt3001.h>
#include <esp_err.h>
#include <driver/gpio.h>
#include <esp_adc.h>


static const char *TAG = "esp_stickbee_rev1-0.c";


adc_oneshot_unit_handle_t bat_adc_handle;
adc_oneshot_unit_init_cfg_t bat_adc_init_config = {
    .unit_id = BAT_ADC,
};
adc_oneshot_chan_cfg_t bat_adc_config = {
    .bitwidth = ADC_BITWIDTH_DEFAULT,
    .atten = BAT_ADC_ATTEN,
};
adc_cali_handle_t bat_adc_cali_handle = NULL;


i2c_master_bus_config_t i2c_bus_config = {
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .i2c_port = I2C_PORT,
    .scl_io_num = I2C_SCL,
    .sda_io_num = I2C_SDA,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
};
i2c_master_bus_handle_t i2c_bus_handle;


i2c_master_dev_handle_t hdc1080_dev_handle;
hdc1080_settings_t hdc1080_settings;
i2c_device_config_t hdc1080_dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = HDC1080_ADDR,
    .scl_speed_hz = 100000,
    .flags.disable_ack_check = true,
};


i2c_master_dev_handle_t opt3001_dev_handle;
opt3001_settings_t opt3001_settings;
i2c_device_config_t opt3001_dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = OPT3001_ADDR,
    .scl_speed_hz = 100000,
    .flags.disable_ack_check = true,
};


bool init_i2c_sensors() {
    // Reset I2C bus pins
    gpio_reset_pin(I2C_SCL);
    gpio_reset_pin(I2C_SDA);

    // Initialize I2C bus
    i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);

    // Add HDC1080 to I2C bus driver
    i2c_master_bus_add_device(i2c_bus_handle, &hdc1080_dev_cfg, &hdc1080_dev_handle);
    hdc1080_settings.device_handle = hdc1080_dev_handle;    // Update device handle
    hdc1080_settings.configuration = HDC1080_CONFIG_TEMP_14BIT_RES_MASK | HDC1080_CONFIG_HUMIDITY_14BIT_RES_MASK | 
                                        HDC1080_CONFIG_HEATER_DISABLE_MASK | HDC1080_CONFIG_TEMP_AND_HUMIDITY_MODE_MASK;

    // Add OPT3001 to I2C bus driver
    i2c_master_bus_add_device(i2c_bus_handle, &opt3001_dev_cfg, &opt3001_dev_handle);
    opt3001_settings.device_handle = opt3001_dev_handle;    // Update device handle
    opt3001_settings.configuration = OPT3001_CONFIG_AUTO_FULLSCALE_RANGE_MASK | OPT3001_CONFIG_CONVERSION_TIME_100MS_MASK | 
                                        OPT3001_CONFIG_CONV_MODE_SINGLESHOT_MASK | OPT3001_CONFIG_LATCH_LATCHED_MASK;

    return true;
}


bool deinit_i2c_sensors() {
    // Delete I2C bus and reset pins
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(hdc1080_dev_handle));
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(opt3001_dev_handle));
    ESP_ERROR_CHECK(i2c_del_master_bus(i2c_bus_handle));
    gpio_reset_pin(I2C_SCL);
    gpio_reset_pin(I2C_SDA);
    return true;
}


bool hdc1080_read_temperature_and_humidity(float *temperature_out, float *humidity_out) {
    // Check for null pointers
    if (temperature_out == NULL || humidity_out == NULL) {
        return false;
    }

    if (HDC1080_Write_Configuration(&hdc1080_settings) == ESP_OK && HDC1080_Read_Vals(&hdc1080_settings, temperature_out, humidity_out) == ESP_OK) {
        ESP_LOGI(TAG, "TEMPERATURE: %.2f°C", *temperature_out);
        ESP_LOGI(TAG, "HUMIDITY: %.2f%%", *humidity_out);
    }
    else {
        return false;
    }

    return true;
}


bool opt3001_read_illuminance(float *illuminance_out) {
    // Check for null pointers
    if (illuminance_out == NULL) {
        ESP_LOGE(TAG, "opt_read_illuminance() NULL pointer");
        return false;
    }

    // Write configuration to trigger single shot conversion
    if (OPT3001_Write_Configuration(&opt3001_settings) != ESP_OK) {
        ESP_LOGE(TAG, "Error writing OPT3001 configuration");
        return false;
    }

    // Wait for conversion to finish
    vTaskDelay(pdMS_TO_TICKS(opt3001_settings.configuration & OPT3001_CONFIG_CONVERSION_TIME_800MS_MASK? 1000 : 200));

    if (OPT3001_Read_Lux(&opt3001_settings, illuminance_out) == ESP_OK) {
        ESP_LOGI(TAG, "LUX: %.2f", *illuminance_out);
    }
    else {
        ESP_LOGE(TAG, "Error reading from OPT3001");
        return false;
    }

    return true;
}


static esp_err_t adc_init(void) {
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&bat_adc_init_config, &bat_adc_handle), TAG, "Failed to create ADC handle");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(bat_adc_handle, BAT_ADC_CH, &bat_adc_config), TAG, "Failed to config ADC channel");

    return adc_calibration_init(BAT_ADC, BAT_ADC_CH, BAT_ADC_ATTEN, &bat_adc_cali_handle) ? ESP_OK : ESP_FAIL;
}

bool adc_read_battery_voltage(int *battery_mv_out) {
    // Check for null pointers
    if (battery_mv_out == NULL) {
        ESP_LOGE(TAG, "adc_read_battery_voltage() NULL pointer");
        return false;
    }

    bool return_value = true;
    int adc_raw = 0;

    // Enable voltage divider
    gpio_set_direction(BAT_ADC_EN_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BAT_ADC_EN_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(ADC_DELAY_MS));

    // Try initializing and deinitializing ADC before each sample (hoping to fix constant 4081 issue)
    if (adc_init() != ESP_OK) {
        ESP_LOGE(TAG, "Error initializing ADC");
        return_value = false;
    }

    // Read ADC
    if(adc_oneshot_read(bat_adc_handle, BAT_ADC_CH, &adc_raw) == ESP_OK) {
        ESP_LOGD(TAG, "ADC value: %d", adc_raw);
    }
    else {
        ESP_LOGE(TAG, "Error reading battery ADC");
        return_value = false;
    }

    // Convert value to voltage
    if (adc_cali_raw_to_voltage(bat_adc_cali_handle, adc_raw, battery_mv_out) == ESP_OK) {
        *battery_mv_out = (int)((float)*battery_mv_out * BAT_ADC_MULTIPLIER);
        ESP_LOGI(TAG, "Battery ADC voltage: %d", *battery_mv_out);
    }
    else {
        ESP_LOGE(TAG, "Error converting battery ADC value");
        return_value = false;
    }

    // Disable voltage divider pin and deinit
    gpio_set_direction(BAT_ADC_EN_GPIO, GPIO_MODE_DISABLE);
    adc_calibration_deinit(bat_adc_cali_handle);
    adc_oneshot_del_unit(bat_adc_handle);

    return return_value;
}
