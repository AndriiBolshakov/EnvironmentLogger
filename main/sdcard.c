#include "sdcard.h"

#include <stdbool.h>
#include <stdio.h>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define SD_MISO_GPIO 4
#define SD_CLK_GPIO 5
#define SD_MOSI_GPIO 6
#define SD_CS_GPIO 7

static const char *TAG = "SDCARD";
static sdmmc_card_t *s_card;

esp_err_t sdcard_init(void)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = SD_MOSI_GPIO,
        .miso_io_num = SD_MISO_GPIO,
        .sclk_io_num = SD_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t err = spi_bus_initialize(
        SPI2_HOST,
        &bus_config,
        SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SPI bus initialization failed: %s",
            esp_err_to_name(err));
        return err;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_GPIO;
    slot_config.host_id = SPI2_HOST;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(
        SDCARD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "SD card mount failed: %s",
            esp_err_to_name(err));
        spi_bus_free(SPI2_HOST);
        return err;
    }

    ESP_LOGI(TAG, "SD card mounted");
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t sdcard_write_string(const char *path, const char *contents)
{
    if (path == NULL || contents == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "a");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for writing", path);
        return ESP_FAIL;
    }

    bool write_failed = fputs(contents, file) == EOF;
    if (fclose(file) != 0) {
        write_failed = true;
    }

    if (write_failed) {
        ESP_LOGE(TAG, "Failed to write %s", path);
        return ESP_FAIL;
    }

    return ESP_OK;
}
