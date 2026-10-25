#pragma once

#include <zigbee_config.h>


void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct);
esp_zb_cluster_list_t *multisensor_cluster_create(esp_zb_multisensor_cfg_t *multisensor);
esp_zb_ep_list_t *multisensor_ep_create(uint8_t endpoint_id, esp_zb_multisensor_cfg_t *multisensor);
esp_err_t esp_zb_power_save_init(void);
void esp_zb_task(void *pvParameters);