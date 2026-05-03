#pragma once
#include <stdint.h>

// Tilt axis motion control — BTS7960 brushed DC + MT6816 absolute encoder

void    motion_init();
void    motion_tick();

void    motion_set_velocity(int16_t vel_cdeg_s);
void    motion_set_position(int32_t pos_cdeg);
void    motion_stop();
void    motion_estop();
void    motion_home();
bool    motion_is_homing();
void    motion_set_settings(uint16_t max_speed_cdeg_s, uint16_t accel_cdeg_s2);
void    motion_clear_can_fault();

int32_t  motion_get_pos_cdeg();
int16_t  motion_get_vel_cdeg_s();
uint8_t  motion_get_flags();
uint16_t motion_get_enc_raw();
