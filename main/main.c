#include <stdio.h>

#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdcard.h"

#define SD_LOG_FILE SDCARD_MOUNT_POINT "/environment.csv"

static const char *TAG = "APP";

#if SD_CARD_DUMP_MODE == 0

#include <stdbool.h>
#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bme280.h"
#include "encoder.h"
#include "i2c.h"
#include "oled.h"
#include "rtc.h"

static i2c_master_dev_handle_t s_rtc;

typedef struct {
    bool enabled;
    oled_page_t page;
} display_state_t;

static display_state_t s_display = {
    .enabled = true,
    .page = OLED_PAGE_DATE_TIME,
};

static void display_change_page(int direction)
{
    int page = (int)s_display.page + direction;
    if (page < 0) {
        page = OLED_PAGE_COUNT - 1;
    } else if (page >= OLED_PAGE_COUNT) {
        page = 0;
    }

    s_display.page = (oled_page_t)page;
    ESP_LOGI(TAG, "Display page: %d", page);
}

static bool rtc_attach(void)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADDR_RTC,
        .scl_speed_hz = I2C_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(
        i2c_bus_get_handle(),
        &config,
        &s_rtc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC attach failed: %s", esp_err_to_name(err));
        return false;
    }

    rtc_clear_clock_halt(s_rtc);
    return true;
}

void app_main(void)
{
    i2c_init();

    bool has_rtc = device_found(ADDR_RTC);
    if (has_rtc) {
        has_rtc = rtc_attach();
    }
    ESP_LOGI(TAG, "DS1307: %s", has_rtc ? "initialized" : "not available");

    uint8_t oled_address = ADDR_OLED;
    bool has_oled = device_found(ADDR_OLED);
    if (!has_oled) {
        oled_address = ADDR_OLED_ALT;
        has_oled = device_found(ADDR_OLED_ALT);
    }
    if (has_oled) {
        oled_init(oled_address);
    }
    ESP_LOGI(TAG, "OLED: %s", has_oled ? "initialized" : "not available");

    uint8_t bme280_address = ADDR_BME280;
    bool has_bme280 = device_found(ADDR_BME280);
    if (!has_bme280) {
        bme280_address = ADDR_BME280_ALT;
        has_bme280 = device_found(ADDR_BME280_ALT);
    }
    if (has_bme280) {
        esp_err_t err = bme280_init(i2c_bus_get_handle(), bme280_address);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "BME280 initialization failed: %s",
                     esp_err_to_name(err));
            has_bme280 = false;
        }
    }
    ESP_LOGI(TAG, "BME280: %s", has_bme280 ? "initialized" : "not available");

    bool has_encoder = encoder_init();
    ESP_LOGI(TAG, "Encoder: %s", has_encoder ? "initialized" : "not available");

    esp_err_t err = sdcard_init();
    if (err != ESP_OK) {
        return;
    }

    TickType_t last_sample_time = xTaskGetTickCount();
    const TickType_t sample_period = pdMS_TO_TICKS(1000);

    while (true) {
        while ((xTaskGetTickCount() - last_sample_time) < sample_period) {
            TickType_t elapsed = xTaskGetTickCount() - last_sample_time;
            if (elapsed >= sample_period) {
                break;
            }

            if (has_encoder) {
                TickType_t remaining = sample_period - elapsed;
                TickType_t wait = encoder_debounce_wait(remaining);
                if (wait > 0) {
                    ulTaskNotifyTake(pdTRUE, wait);
                }
                encoder_events_t events = encoder_take_events();
                while (events.rotations > 0) {
                    ESP_LOGI(TAG, "Encoder rotated: +1");
                    display_change_page(1);
                    events.rotations--;
                }
                while (events.rotations < 0) {
                    ESP_LOGI(TAG, "Encoder rotated: -1");
                    display_change_page(-1);
                    events.rotations++;
                }
                if (encoder_confirm_button_press()) {
                    ESP_LOGI(TAG, "Encoder button pressed");
                    s_display.enabled = !s_display.enabled;
                    ESP_LOGI(TAG, "Display: %s",
                             s_display.enabled ? "enabled" : "disabled");
                    if (!s_display.enabled && has_oled) {
                        oled_clear();
                    }
                }
            } else {
                vTaskDelay(sample_period - elapsed);
            }
        }
        last_sample_time += sample_period;
        int64_t elapsed_seconds = esp_timer_get_time() / 1000000;

        float temperature_c = 0.0f;
        float pressure_hpa = 0.0f;
        float humidity_percent = 0.0f;
        bool has_bme280_reading = false;

        if (has_bme280) {
            float pressure_pa;
            err = bme280_read(
                &temperature_c,
                &pressure_pa,
                &humidity_percent);
            if (err == ESP_OK) {
                pressure_hpa = pressure_pa / 100.0f;
                has_bme280_reading = true;
            } else {
                ESP_LOGE(TAG, "BME280 read failed: %s", esp_err_to_name(err));
            }
        }

        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;
        bool has_datetime = false;

        if (has_rtc) {
            err = rtc_get_datetime(
                &year,
                &month,
                &day,
                &hour,
                &minute,
                &second,
                s_rtc);
            if (err == ESP_OK) {
                has_datetime = true;
            } else {
                ESP_LOGE(TAG, "RTC read failed: %s", esp_err_to_name(err));
            }
        }

        if (has_oled && s_display.enabled) {
            const oled_datetime_t display_data = {
                .page = s_display.page,
                .year = year,
                .month = month,
                .day = day,
                .hour = hour,
                .minute = minute,
                .second = second,
                .show_colons = true,
                .temperature_c = temperature_c,
                .pressure_hpa = pressure_hpa,
                .humidity_percent = humidity_percent,
                .has_bme280_reading = has_bme280_reading,
            };
            if (s_display.page != OLED_PAGE_DATE_TIME || has_datetime) {
                oled_show_datetime(display_data);
            }
        }

        char timestamp[32];
        if (has_datetime) {
            snprintf(
                timestamp,
                sizeof(timestamp),
                "%04d-%02d-%02d %02d:%02d:%02d",
                year,
                month,
                day,
                hour,
                minute,
                second);
        } else {
            snprintf(
                timestamp,
                sizeof(timestamp),
                "uptime=%llds",
                (long long)elapsed_seconds);
        }

        char temperature[16] = "n/a";
        char pressure[16] = "n/a";
        char humidity[16] = "n/a";
        if (has_bme280_reading) {
            snprintf(temperature, sizeof(temperature), "%.2f", temperature_c);
            snprintf(pressure, sizeof(pressure), "%.2f", pressure_hpa);
            snprintf(humidity, sizeof(humidity), "%.2f", humidity_percent);
        }

        char record[128];
        int record_length = snprintf(
            record,
            sizeof(record),
            "%s,%s,%s,%s\n",
            timestamp,
            temperature,
            pressure,
            humidity);
        if (record_length < 0 || (size_t)record_length >= sizeof(record)) {
            ESP_LOGE(TAG, "Failed to format environment record");
            continue;
        }

        err = sdcard_write_string(SD_LOG_FILE, record);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to append environment record");
            continue;
        }

        ESP_LOGI(TAG, "Environment record: %s", record);
    }
}

#elif SD_CARD_DUMP_MODE == 1

#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>

static esp_err_t dump_log_file(const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for reading", path);
        return ESP_FAIL;
    }

    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    while ((length = getline(&line, &capacity, file)) != -1) {
        while (length > 0 &&
               (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        ESP_LOGI(TAG, "%s", line);
    }

    bool read_failed = ferror(file);
    free(line);
    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "Failed to close %s after reading", path);
        return ESP_FAIL;
    }
    if (read_failed) {
        ESP_LOGE(TAG, "Failed while reading %s", path);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void app_main(void)
{
    esp_err_t err = sdcard_init();
    if (err != ESP_OK) {
        return;
    }

    err = dump_log_file(SD_LOG_FILE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to dump log file");
    }
}

#else
#error "SD_CARD_DUMP_MODE must be 0 or 1"
#endif
