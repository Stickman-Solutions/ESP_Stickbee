#pragma once

// GPIO assignments
#define PIR_GPIO            GPIO_NUM_3
#define LEDP_GPIO           GPIO_NUM_23
#define LED0_GPIO           GPIO_NUM_21
#define LED1_GPIO           GPIO_NUM_22
#define IDENTIFY_LED_GPIO   LED0_GPIO
#define NETWORK_LED_GPIO    LED1_GPIO
#define BUTT0_GPIO          GPIO_NUM_7
#define BAT_ADC_EN_GPIO     GPIO_NUM_20

// I2C stuff for HDC1080 and OPT3001
#define I2C_SCL             GPIO_NUM_11
#define I2C_SDA             GPIO_NUM_10
#define I2C_PORT            I2C_NUM_0
#define HDC1080_ADDR        0x40
#define OPT3001_ADDR        0x45

// ADC configuration
#define BAT_ADC             ADC_UNIT_1
#define BAT_ADC_CH          ADC_CHANNEL_2   // GPIO 2
#define BAT_ADC_ATTEN       ADC_ATTEN_DB_0  // No attenuation needed if using AA battery and 1/2 voltage divider
#define ADC_DELAY_MS        0               // Might be needed if using cap on ADC pin
