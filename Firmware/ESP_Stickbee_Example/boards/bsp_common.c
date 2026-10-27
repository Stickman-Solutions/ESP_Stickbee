#include <bsp_common.h>



bool init_sensors() {
    return init_i2c_sensors();
}


bool deinit_sensors() {
    return deinit_i2c_sensors();
}


bool read_temperature_and_humidity(float *temperature_out, float *humidity_out) {
    return hdc1080_read_temperature_and_humidity(temperature_out, humidity_out);
}


bool read_illuminance(float *illuminance_out) {
    return opt3001_read_illuminance(illuminance_out);
}


bool read_battery_voltage(float *battery_voltage_out) {
    return false;
}
