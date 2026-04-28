#pragma once
#include <stdint.h>

// Pan axis motion control — TMC2209 stepper + MT6816 absolute encoder
// Encoder is on the OUTPUT shaft (after 26:1 gearbox), so position is direct.

void    motion_init();
void    motion_tick();           // call every loop() — handles step generation

// Commands from CAN
void    motion_set_velocity(int16_t vel_cdeg_s);
void    motion_set_position(int32_t pos_cdeg);
void    motion_stop();
void    motion_estop();
void    motion_home();           // trigger homing sweep to hall sensor
bool    motion_is_homing();      // true while sweep is in progress
void    motion_set_settings(uint16_t max_speed_cdeg_s, uint16_t accel_cdeg_s2);
void    motion_clear_can_fault();   // clear fault caused by CAN timeout on reconnect

// State readback
int32_t  motion_get_pos_cdeg();
int16_t  motion_get_vel_cdeg_s();
uint8_t  motion_get_flags();
uint16_t motion_get_enc_raw();      // POS_FLAG_* bitmask
