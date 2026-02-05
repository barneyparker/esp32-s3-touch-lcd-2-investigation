#include "step_counter.h"
#include "storage_manager.h"
#include "app_state.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "network/ntp_sync.h"
#include <time.h>
#include <string.h>

// Forward declarations for functions used by the worker
esp_err_t step_counter_increment(void);
esp_err_t step_counter_add_to_backlog(uint32_t timestamp);

static const char *TAG = "step_counter";

// Step counter state
static uint32_t step_count = 0;
static uint8_t backlog_size = 0;
static TickType_t last_step_time = 0;
static const TickType_t DEBOUNCE_TICKS = pdMS_TO_TICKS(80);  // 80ms debounce

static SemaphoreHandle_t step_mutex = NULL;
// Queue used to notify worker task of steps (ISR -> task)
static QueueHandle_t step_event_queue = NULL;

// Queue for buffering steps when offline (max 255 steps for uint8_t index)
#define MAX_STEP_BACKLOG 255
static uint32_t step_timestamps[MAX_STEP_BACKLOG];
static uint8_t backlog_idx = 0;

// ISR handler for GPIO interrupt
static void IRAM_ATTR step_isr_handler(void *arg)
{
    TickType_t now = xTaskGetTickCountFromISR();

    // Debounce check
    if (now - last_step_time < DEBOUNCE_TICKS) {
        return;
    }

    last_step_time = now;

    // Notify worker task of step (keep ISR short)
    if (step_event_queue) {
        uint8_t ev = 1;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(step_event_queue, &ev, &xHigherPriorityTaskWoken);
        // Lightweight ISR-safe log to aid debugging
        ESP_DRAM_LOGW(TAG, "STEP IRQ queued");
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// Worker task: dequeues ISR notifications, timestamps (using NTP when available),
// increments the persistent count and appends a timestamp to the backlog.
static void step_worker_task(void *arg)
{
    (void)arg;
    uint8_t ev;

    for (;;) {
        if (xQueueReceive(step_event_queue, &ev, portMAX_DELAY) == pdTRUE) {
            uint32_t ts = 0;

            if (ntp_sync_is_synced()) {
                ts = (uint32_t)time(NULL);
            } else {
                if (ntp_sync_wait_for_sync(5000) == ESP_OK) {
                    ts = (uint32_t)time(NULL);
                } else {
                    ts = (uint32_t)xTaskGetTickCount();
                }
            }

            esp_err_t inc_ret = step_counter_increment();
            if (inc_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to increment step count: %s", esp_err_to_name(inc_ret));
            }

            esp_err_t add_ret = step_counter_add_to_backlog(ts);
            if (add_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to add step to backlog: %s", esp_err_to_name(add_ret));
            }
        }
    }
}

esp_err_t step_counter_init(void)
{
    // Create mutex for thread-safe step counting
    step_mutex = xSemaphoreCreateMutex();
    if (!step_mutex) {
        ESP_LOGE(TAG, "Failed to create step counter mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create event queue for ISR -> worker notifications
    step_event_queue = xQueueCreate(64, sizeof(uint8_t));
    if (!step_event_queue) {
        ESP_LOGE(TAG, "Failed to create step event queue");
        vSemaphoreDelete(step_mutex);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Step event queue created (len=64)");

    // Load step count from NVS
    uint32_t stored_count = 0;
    esp_err_t ret = storage_get_u32("steps", "count", &stored_count);
    if (ret == ESP_OK) {
        step_count = stored_count;
        ESP_LOGI(TAG, "Loaded step count from NVS: %lu", step_count);
    } else {
        ESP_LOGI(TAG, "No step count in NVS, starting from 0");
    }

    // Load backlog from NVS if present
    uint8_t buffer[MAX_STEP_BACKLOG * 4];
    size_t blob_size = sizeof(buffer);
    ret = storage_get_blob("steps", "backlog", buffer, &blob_size);
    if (ret == ESP_OK && blob_size > 0) {
        backlog_idx = blob_size / 4;
        if (backlog_idx >= MAX_STEP_BACKLOG) {
            backlog_idx = MAX_STEP_BACKLOG - 1;
        }
        memcpy(step_timestamps, buffer, backlog_idx * 4);
        backlog_size = backlog_idx;
        ESP_LOGI(TAG, "Loaded %d buffered steps from NVS", backlog_idx);
    }

    // Configure GPIO for step detection on GPIO 18
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 18),  // GPIO 18
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // Trigger on falling edge (magnet switch)
    };

    esp_err_t gpio_ret = gpio_config(&io_conf);
    if (gpio_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(gpio_ret));
        vSemaphoreDelete(step_mutex);
        return gpio_ret;
    }

    // Install ISR service
    gpio_ret = gpio_install_isr_service(ESP_INTR_FLAG_LOWMED);
    if (gpio_ret != ESP_OK && gpio_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(gpio_ret));
        vSemaphoreDelete(step_mutex);
        return gpio_ret;
    }

    // Register ISR handler for GPIO 18
    gpio_ret = gpio_isr_handler_add(18, step_isr_handler, NULL);
    if (gpio_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GPIO ISR handler: %s", esp_err_to_name(gpio_ret));
        vSemaphoreDelete(step_mutex);
        return gpio_ret;
    }

    // Create worker task to process steps and add timestamps to backlog
    if (xTaskCreate(step_worker_task, "step_worker", 4096, NULL, tskIDLE_PRIORITY + 5, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Failed to create step worker task");
    } else {
        ESP_LOGI(TAG, "Step worker task created");
    }

    ESP_LOGI(TAG, "Step counter initialized on GPIO 18");
    return ESP_OK;
}

uint32_t step_counter_get_count(void)
{
    if (!step_mutex) return 0;

    xSemaphoreTake(step_mutex, portMAX_DELAY);
    uint32_t count = step_count;
    xSemaphoreGive(step_mutex);

    return count;
}

esp_err_t step_counter_set_count(uint32_t count)
{
    if (!step_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(step_mutex, portMAX_DELAY);
    step_count = count;
    xSemaphoreGive(step_mutex);

    // Persist to NVS
    esp_err_t ret = storage_set_u32("steps", "count", count);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save step count to NVS: %s", esp_err_to_name(ret));
    }

    app_state_set_steps(count, backlog_size);
    return ret;
}

esp_err_t step_counter_increment(void)
{
    uint32_t new_count = step_counter_get_count() + 1;
    return step_counter_set_count(new_count);
}

uint8_t step_counter_get_backlog_size(void)
{
    if (!step_mutex) return 0;

    xSemaphoreTake(step_mutex, portMAX_DELAY);
    uint8_t size = backlog_size;
    xSemaphoreGive(step_mutex);

    return size;
}

esp_err_t step_counter_add_to_backlog(uint32_t timestamp)
{
    if (!step_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(step_mutex, portMAX_DELAY);

    if (backlog_idx >= MAX_STEP_BACKLOG) {
        xSemaphoreGive(step_mutex);
        return ESP_ERR_NO_MEM;  // Backlog is full
    }

    step_timestamps[backlog_idx++] = timestamp;
    backlog_size = backlog_idx;

    xSemaphoreGive(step_mutex);

    // Persist backlog to NVS
    return storage_set_blob("steps", "backlog", step_timestamps, backlog_idx * 4);
}

esp_err_t step_counter_flush_backlog(void)
{
    if (!step_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(step_mutex, portMAX_DELAY);
    backlog_idx = 0;
    backlog_size = 0;
    xSemaphoreGive(step_mutex);

    // Clear backlog from NVS
    return storage_delete("steps", "backlog");
}

esp_err_t step_counter_get_next_buffered_step(uint32_t *timestamp)
{
    if (!step_mutex || !timestamp) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(step_mutex, portMAX_DELAY);

    if (backlog_size == 0) {
        xSemaphoreGive(step_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    *timestamp = step_timestamps[0];
    xSemaphoreGive(step_mutex);

    return ESP_OK;
}

esp_err_t step_counter_remove_first_buffered_step(void)
{
    if (!step_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(step_mutex, portMAX_DELAY);

    if (backlog_size == 0) {
        xSemaphoreGive(step_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // Shift all timestamps down by one
    for (int i = 1; i < backlog_idx; i++) {
        step_timestamps[i - 1] = step_timestamps[i];
    }

    backlog_idx--;
    backlog_size = backlog_idx;

    xSemaphoreGive(step_mutex);

    // Persist updated backlog to NVS
    if (backlog_idx > 0) {
        return storage_set_blob("steps", "backlog", step_timestamps, backlog_idx * 4);
    } else {
        return storage_delete("steps", "backlog");
    }
}
