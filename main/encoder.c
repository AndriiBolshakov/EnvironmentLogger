#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "encoder.h"

#define ENCODER_SW_GPIO GPIO_NUM_40
#define ENCODER_DT_GPIO GPIO_NUM_41
#define ENCODER_CLK_GPIO GPIO_NUM_42
#define ENCODER_BUTTON_DEBOUNCE pdMS_TO_TICKS(30)

static const char *TAG = "ENCODER";

typedef struct {
    uint8_t previous_state;
    int8_t transition_count;
    int previous_switch_level;
    int button_candidate_level;
    int button_stable_level;
    TickType_t button_candidate_since;
    bool button_candidate_pending;
    int32_t pending_rotations;
} encoder_state_t;

static encoder_state_t s_encoder;
static portMUX_TYPE s_encoder_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_encoder_task;

static void encoder_gpio_isr(void *arg);

bool encoder_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << ENCODER_SW_GPIO)
                      | (1ULL << ENCODER_DT_GPIO)
                      | (1ULL << ENCODER_CLK_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Encoder GPIO initialization failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    int dt = gpio_get_level(ENCODER_DT_GPIO);
    int clk = gpio_get_level(ENCODER_CLK_GPIO);
    int sw = gpio_get_level(ENCODER_SW_GPIO);
    s_encoder.previous_state = (uint8_t)((dt << 1) | clk);
    s_encoder.previous_switch_level = sw;
    s_encoder.button_candidate_level = sw;
    s_encoder.button_stable_level = sw;

    err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Encoder ISR service installation failed: %s",
                 esp_err_to_name(err));
        return false;
    }

    s_encoder_task = xTaskGetCurrentTaskHandle();
    const gpio_num_t pins[] = {
        ENCODER_DT_GPIO,
        ENCODER_CLK_GPIO,
        ENCODER_SW_GPIO,
    };
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        err = gpio_isr_handler_add(pins[i], encoder_gpio_isr, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Encoder ISR registration failed for GPIO %d: %s",
                     pins[i], esp_err_to_name(err));
            while (i > 0) {
                i--;
                gpio_isr_handler_remove(pins[i]);
            }
            return false;
        }
    }

    return true;
}

static void encoder_gpio_isr(void *arg)
{
    (void)arg;

    static const int8_t transitions[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0,
    };

    BaseType_t higher_priority_task_woken = pdFALSE;
    portENTER_CRITICAL_ISR(&s_encoder_mux);
    int dt = gpio_get_level(ENCODER_DT_GPIO);
    int clk = gpio_get_level(ENCODER_CLK_GPIO);
    uint8_t current_state = (uint8_t)((dt << 1) | clk);
    uint8_t transition = (uint8_t)((s_encoder.previous_state << 2)
                                   | current_state);
    s_encoder.transition_count += transitions[transition];
    s_encoder.previous_state = current_state;

    if (s_encoder.transition_count >= 4) {
        if (s_encoder.pending_rotations < INT32_MAX) {
            s_encoder.pending_rotations++;
        }
        s_encoder.transition_count = 0;
    } else if (s_encoder.transition_count <= -4) {
        if (s_encoder.pending_rotations > INT32_MIN) {
            s_encoder.pending_rotations--;
        }
        s_encoder.transition_count = 0;
    }

    int sw = gpio_get_level(ENCODER_SW_GPIO);
    if (sw != s_encoder.previous_switch_level) {
        s_encoder.previous_switch_level = sw;
        s_encoder.button_candidate_level = sw;
        s_encoder.button_candidate_since = xTaskGetTickCountFromISR();
        s_encoder.button_candidate_pending = true;
    }

    bool has_events = s_encoder.pending_rotations != 0
                   || s_encoder.button_candidate_pending;
    portEXIT_CRITICAL_ISR(&s_encoder_mux);
    if (has_events) {
        vTaskNotifyGiveFromISR(s_encoder_task, &higher_priority_task_woken);
        if (higher_priority_task_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

encoder_events_t encoder_take_events(void)
{
    encoder_events_t events;
    portENTER_CRITICAL(&s_encoder_mux);
    events.rotations = s_encoder.pending_rotations;
    s_encoder.pending_rotations = 0;
    portEXIT_CRITICAL(&s_encoder_mux);
    return events;
}

bool encoder_confirm_button_press(void)
{
    bool button_pressed = false;
    TickType_t now = xTaskGetTickCount();

    portENTER_CRITICAL(&s_encoder_mux);
    if (s_encoder.button_candidate_pending
        && (now - s_encoder.button_candidate_since)
               >= ENCODER_BUTTON_DEBOUNCE) {
        int current_level = gpio_get_level(ENCODER_SW_GPIO);
        if (current_level != s_encoder.button_candidate_level) {
            s_encoder.button_candidate_level = current_level;
            s_encoder.button_candidate_since = now;
        } else {
            s_encoder.button_candidate_pending = false;
            if (current_level != s_encoder.button_stable_level) {
                s_encoder.button_stable_level = current_level;
                button_pressed = current_level == 0;
            }
        }
    }
    portEXIT_CRITICAL(&s_encoder_mux);

    return button_pressed;
}

TickType_t encoder_debounce_wait(TickType_t maximum_wait)
{
    TickType_t wait = maximum_wait;
    TickType_t now = xTaskGetTickCount();

    portENTER_CRITICAL(&s_encoder_mux);
    if (s_encoder.button_candidate_pending) {
        TickType_t elapsed = now - s_encoder.button_candidate_since;
        TickType_t debounce_remaining =
            elapsed >= ENCODER_BUTTON_DEBOUNCE
                ? 0
                : ENCODER_BUTTON_DEBOUNCE - elapsed;
        if (debounce_remaining < wait) {
            wait = debounce_remaining;
        }
    }
    portEXIT_CRITICAL(&s_encoder_mux);

    return wait;
}
