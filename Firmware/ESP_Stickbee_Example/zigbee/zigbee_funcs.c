#include <zigbee_funcs.h>
#include <esp_stickbee_rev1-0.h>
#include <esp_pm.h>
#include <driver/gpio.h>
#include <esp_check.h>


#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE in idf.py menuconfig to compile sensor (End Device) source code.
#endif

static const char *TAG = "zigbee_funcs.c";

uint8_t zb_started_flag = 0;            // Flag is set after Zigbee stack is initialized
int zb_network_status = 0;              // 0 for unjoined, 1 for joined, -1 for left
uint8_t zb_unavailable_alarm_flag = 0;  // 0 if alarm has not been set, 1 if alarm is set
uint8_t zb_steering_alarm_flag = 0;     // 0 if alarm has not been set, 1 if alarm is set
int16_t zb_bat_voltage, zb_temperature;
uint16_t zb_humidity, zb_illuminance;
uint16_t zb_identify_time = 0;
uint8_t zb_currently_identifying = 0;
uint8_t leds_enabled = 0;       // When set, allow network LED to turn on


static void deinit_and_restart() {
    //i2c_master_bus_rm_device(hdc1080_dev_handle);  // Should be redundant if deleting the bus
    //i2c_master_bus_rm_device(opt3001_dev_handle);  // Should be redundant if deleting the bus
    //i2c_del_master_bus(i2c_bus_handle);
    //adc_calibration_deinit(bat_adc_cali_handle);
    //adc_oneshot_del_unit(bat_adc_handle);
    gpio_uninstall_isr_service();
    gpio_hold_dis(LED0_GPIO);
    gpio_hold_dis(LED1_GPIO);
    gpio_hold_dis(LEDP_GPIO);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}


static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, ,
                        TAG, "Failed to start Zigbee bdb commissioning");
}


static void handle_zigbee_unavilable(uint8_t input) {
    zb_unavailable_alarm_flag = 0;
    if (zb_network_status != 1) {   // If the device hasn't joined a zigbee network
        //esp_zb_factory_reset();
        deinit_and_restart();
        //bdb_start_top_level_commissioning_cb(ESP_ZB_BDB_MODE_NETWORK_STEERING);
    }
}


void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p     = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted");
            }
        } else {
            /* commissioning failed */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
            // This is caused when the parent node can't be found. Keep resetting until parent node is found: https://github.com/espressif/esp-zigbee-sdk/issues/169#issuecomment-1849540638
            deinit_and_restart();
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        zb_steering_alarm_flag = 0;
        if (err_status == ESP_OK) {
            zb_network_status = 1;
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    // CUSTOM CODE:
    // Handle leaving the network signal
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        esp_zb_zdo_signal_leave_params_t *leave_params = (esp_zb_zdo_signal_leave_params_t *)esp_zb_app_signal_get_params(p_sg_p);
        if (leave_params->leave_type == ESP_ZB_NWK_LEAVE_TYPE_RESET) {
            ESP_LOGI(TAG, "Leave signal received (RESET)");
        }
        else if (leave_params->leave_type == ESP_ZB_NWK_LEAVE_TYPE_REJOIN) {
            ESP_LOGI(TAG, "Leave signal received (REJOIN)");
        }
        ESP_LOGI(TAG, "Restarting network steering");

        zb_network_status = 0;

        // Restart commissioning to rejoin a network
        esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);

        // Set alarm to handle if network isn't rejoined
        if (zb_unavailable_alarm_flag == 0) {
            zb_unavailable_alarm_flag = 1;
            ESP_LOGI(TAG, "Zigbee unavailable alarm set");
            esp_zb_scheduler_alarm((esp_zb_callback_t)handle_zigbee_unavilable, 0, 60*1000);
        }
        //deinit_and_restart();
        break;
    // Handle unavailable signal
    case ESP_ZB_ZDO_DEVICE_UNAVAILABLE:
        zb_network_status = 0;
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
        // Set alarm to handle zigbee unavailable error if it hasn't been set yet
        if (zb_unavailable_alarm_flag == 0) {
            zb_unavailable_alarm_flag = 1;
            ESP_LOGI(TAG, "Zigbee unavailable alarm set");
            esp_zb_scheduler_alarm((esp_zb_callback_t)handle_zigbee_unavilable, 0, 60*1000);
        }
        break;
    // Handle being able to sleep
    case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
        ESP_LOGI(TAG, "Zigbee can sleep");
        esp_zb_sleep_now();
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }

    // Handle network LED
    if (leds_enabled == 1) {
        if (esp_zb_bdb_dev_joined()) {
            gpio_set_direction(NETWORK_LED_GPIO, GPIO_MODE_OUTPUT);
            gpio_hold_dis(NETWORK_LED_GPIO);
            gpio_set_level(NETWORK_LED_GPIO, 0);
            gpio_hold_en(NETWORK_LED_GPIO);
        }
        else {
            gpio_hold_dis(NETWORK_LED_GPIO);
            gpio_set_direction(NETWORK_LED_GPIO, GPIO_MODE_DISABLE);
        }
    }
}


esp_zb_cluster_list_t *multisensor_cluster_create(esp_zb_multisensor_cfg_t *multisensor)
{
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(&(multisensor->basic_cfg));

    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, MANUFACTURER_NAME));
    ESP_ERROR_CHECK(esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, MODEL_IDENTIFIER));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_identify_cluster_create(&(multisensor->identify_cfg)), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_temperature_meas_cluster(cluster_list, esp_zb_temperature_meas_cluster_create(&(multisensor->temp_meas_cfg)), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_humidity_meas_cluster(cluster_list, esp_zb_humidity_meas_cluster_create(&(multisensor->humidity_meas_cfg)), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_illuminance_meas_cluster(cluster_list, esp_zb_illuminance_meas_cluster_create(&(multisensor->light_meas_cfg)), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_occupancy_sensing_cluster(cluster_list, esp_zb_occupancy_sensing_cluster_create(&(multisensor->pir_sens_cfg)), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_on_off_cluster(cluster_list, esp_zb_on_off_cluster_create(&(multisensor->pir_led_on_off_cfg)), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    
    // Create custom attributes
    esp_zb_attribute_list_t *custom_electrical_attr_list = esp_zb_zcl_attr_list_create(CUSTOM_ELECTRICAL_CLUSTER_ID);
    ESP_ERROR_CHECK(esp_zb_custom_cluster_add_custom_attr(custom_electrical_attr_list, CUSTOM_ELECTRICAL_CLUSTER_BATTERY_ATTR_ID, ESP_ZB_ZCL_ATTR_TYPE_S16, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY |
                                                                                                                           ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &zb_bat_voltage));

    // Create custom clusters
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_custom_cluster(cluster_list, custom_electrical_attr_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    return cluster_list;
}


esp_zb_ep_list_t *multisensor_ep_create(uint8_t endpoint_id, esp_zb_multisensor_cfg_t *multisensor)
{
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = endpoint_id,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_CUSTOM_ATTR_DEVICE_ID,
        .app_device_version = 0
    };
    esp_zb_ep_list_add_ep(ep_list, multisensor_cluster_create(multisensor), endpoint_config);
    return ep_list;
}


esp_err_t esp_zb_power_save_init(void)
{
    esp_err_t rc = ESP_OK;
#ifdef CONFIG_PM_ENABLE
    int cur_cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    esp_pm_config_t pm_config = {
        .max_freq_mhz = cur_cpu_freq_mhz,
        .min_freq_mhz = cur_cpu_freq_mhz,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#endif
    };
    rc = esp_pm_configure(&pm_config);
#endif
    return rc;
}


static void identify_led_toggler(uint8_t led_state) {
    gpio_hold_dis(IDENTIFY_LED_GPIO);
    gpio_set_level(IDENTIFY_LED_GPIO, led_state);
    gpio_hold_en(IDENTIFY_LED_GPIO);

    if (zb_currently_identifying == 1) {
        esp_zb_scheduler_alarm((esp_zb_callback_t)identify_led_toggler, !led_state, 100);
    }
}


static void identify_cluster_handler(uint8_t last_alarm_call) {

    // Update identify time cluster
    zb_identify_time--;
    esp_zb_lock_acquire(portMAX_DELAY);
    esp_zb_zcl_set_attribute_val(HA_ESP_SENSOR_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID, &zb_identify_time, false);
    esp_zb_lock_release();

    // For the last alarm call to this function, disable LED pin
    if (last_alarm_call) {
        gpio_hold_dis(IDENTIFY_LED_GPIO);
        gpio_set_direction(IDENTIFY_LED_GPIO, GPIO_MODE_DISABLE);
        zb_currently_identifying = 0;
        esp_zb_scheduler_alarm_cancel((esp_zb_callback_t)identify_led_toggler, 0);  // Cancel alarm just in case
    }
    else if (zb_identify_time > 0) {
        // Schedule next alarm normally
        esp_zb_scheduler_alarm((esp_zb_callback_t)identify_cluster_handler, (zb_identify_time==1? 1 : 0), 1000);
    }
    
}


static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;
    bool light_state = 0;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);
    ESP_LOGI(TAG, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);
    if (message->info.dst_endpoint == HA_ESP_SENSOR_ENDPOINT) {
        // Handle on off cluster
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
                light_state = message->attribute.data.value ? *(bool *)message->attribute.data.value : light_state;
                ESP_LOGI(TAG, "PIR LED set to %s", light_state ? "on" : "off");
                if (light_state == 0) {
                    gpio_hold_dis(LEDP_GPIO);
                    gpio_set_direction(LEDP_GPIO, GPIO_MODE_DISABLE);
                }
                else {
                    gpio_set_direction(LEDP_GPIO, GPIO_MODE_OUTPUT);
                    gpio_set_level(LEDP_GPIO, 0);
                    gpio_hold_en(LEDP_GPIO);
                }
            }
        }
        // Handle identify cluster
        else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U16) {
                // Make sure not currently in identify mode
                if (zb_currently_identifying == 0) {
                   zb_identify_time = *(uint16_t*)(message->attribute.data.value);
                    ESP_LOGI(TAG, "Identify time set to %d seconds", zb_identify_time);

                    // Turn on identify LED initially
                    gpio_set_direction(IDENTIFY_LED_GPIO, GPIO_MODE_OUTPUT);
                    gpio_hold_dis(IDENTIFY_LED_GPIO);
                    gpio_set_level(IDENTIFY_LED_GPIO, 0);
                    gpio_hold_en(IDENTIFY_LED_GPIO);

                    zb_currently_identifying = 1;
                    esp_zb_scheduler_alarm((esp_zb_callback_t)identify_led_toggler, 1, 100);
                    esp_zb_scheduler_alarm((esp_zb_callback_t)identify_cluster_handler, (zb_identify_time==1? 1 : 0), 1000);
                }
            }
        }
    }
    return ret;
}


static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    default:
        ESP_LOGI(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}


void esp_zb_task(void *pvParameters)
{
    // Initialize Zigbee stack
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_sleep_enable(true);
    //esp_zb_sleep_set_threshold(ZIGBEE_SLEEP_TIME_MS); // Unused
    esp_zb_init(&zb_nwk_cfg);

    // Create customized temperature sensor endpoint
    esp_zb_multisensor_cfg_t sensor_cfg;

    // Set some default values
    sensor_cfg.pir_sens_cfg.occupancy = 0;                                  // Unoccupied
    sensor_cfg.pir_sens_cfg.sensor_type = 0;                                // PIR
    sensor_cfg.basic_cfg.power_source = 0x03;                               // For battery
    sensor_cfg.temp_meas_cfg.measured_value = zb_temperature;
    sensor_cfg.humidity_meas_cfg.measured_value = zb_humidity;
    sensor_cfg.light_meas_cfg.measured_value = zb_illuminance;
    sensor_cfg.electrical_meas_cfg.measured_battery_value = zb_bat_voltage;
    sensor_cfg.pir_led_on_off_cfg.on_off = 0;

    esp_zb_ep_list_t *esp_zb_sensor_ep = multisensor_ep_create(HA_ESP_SENSOR_ENDPOINT, &sensor_cfg);

    // Register the device
    esp_zb_device_register(esp_zb_sensor_ep);

    // Register action handler function
    esp_zb_core_action_handler_register(zb_action_handler);

    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));

    zb_started_flag = 1;    // Set zigbee started flag

    esp_zb_main_loop_iteration();
}
