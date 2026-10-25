
#pragma once

#ifndef SD_CARD_DUMP_MODE
#define SD_CARD_DUMP_MODE 0
#endif

#include "driver/gpio.h"
#include "driver/i2c.h"

// I2C configuration
#define I2C_PORT           I2C_NUM_0
#define I2C_SDA_GPIO       GPIO_NUM_8
#define I2C_SCL_GPIO       GPIO_NUM_9
#define I2C_HZ             100000
#define I2C_TIMEOUT_MS     200

// Device addresses
#define ADDR_OLED          0x3C
#define ADDR_OLED_ALT      0x3D
#define ADDR_RTC           0x68
#define ADDR_BME280        0x76
#define ADDR_BME280_ALT    0x77
