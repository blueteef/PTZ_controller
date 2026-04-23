#pragma once
#include <stdint.h>

void sensors_init();

// Each returns true if data is fresh/valid
bool sensors_get_imu(int16_t &roll_cdeg, int16_t &pitch_cdeg, int16_t &yaw_cdeg);
bool sensors_get_mag(int16_t &hdg_cdeg);
bool sensors_get_env(int16_t &temp_cdeg, uint16_t &press_hPa);
bool sensors_get_power(uint16_t &voltage_mv, int16_t &current_ma);
