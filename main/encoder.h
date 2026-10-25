#ifndef ENCODER_H
#define ENCODER_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

typedef struct {
    int32_t rotations;
} encoder_events_t;

bool encoder_init(void);
encoder_events_t encoder_take_events(void);
bool encoder_confirm_button_press(void);
TickType_t encoder_debounce_wait(TickType_t maximum_wait);

#endif
