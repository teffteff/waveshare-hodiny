#pragma once
#include <Wire.h>

#include "Board.h"

#if HODINY_BOARD_LCD7
#define I2C_SCL_PIN       9
#define I2C_SDA_PIN       8
#else
#define I2C_SCL_PIN       7
#define I2C_SDA_PIN       15
#endif


void I2C_Init(void);

bool I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length);
bool I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length);