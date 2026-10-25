#ifndef SDCARD_H
#define SDCARD_H

#include "esp_err.h"

#define SDCARD_MOUNT_POINT "/sdcard"

esp_err_t sdcard_init(void);
esp_err_t sdcard_write_string(const char *path, const char *contents);

#endif
