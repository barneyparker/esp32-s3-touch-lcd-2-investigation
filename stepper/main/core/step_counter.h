#ifndef STEP_COUNTER_H
#define STEP_COUNTER_H
#include "esp_err.h"
#include <stdint.h>

esp_err_t step_counter_init(void);
uint32_t step_counter_get_count(void);
uint8_t step_counter_get_backlog_size(void);
esp_err_t step_counter_get_next_buffered_step(uint32_t *timestamp);
esp_err_t step_counter_remove_first_buffered_step(void);

#endif
