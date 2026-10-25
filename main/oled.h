#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OLED_PAGE_DATE_TIME,
    OLED_PAGE_TEMPERATURE,
    OLED_PAGE_PRESSURE,
    OLED_PAGE_HUMIDITY,
    OLED_PAGE_COUNT,
} oled_page_t;

typedef struct {
    oled_page_t page;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    bool show_colons;
    float temperature_c;
    float pressure_hpa;
    float humidity_percent;
    bool has_bme280_reading;
} oled_datetime_t;

void oled_init(uint8_t address);
void oled_clear(void);
void oled_show_datetime(oled_datetime_t datetime);