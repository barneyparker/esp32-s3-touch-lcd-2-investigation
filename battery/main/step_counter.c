#include "step_counter.h"
#include "websocket_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "step_counter";

// Configuration
#define STEP_GPIO 18
#define MAX_BUFFERED_STEPS 100
#define DEBOUNCE_MS 80

// Step buffer
static uint64_t step_buffer[MAX_BUFFERED_STEPS];
static volatile uint8_t step_buffer_size = 0;
static volatile uint8_t step_buffer_read_idx = 0;
static volatile uint8_t step_buffer_write_idx = 0;

// Total step counter
static volatile uint32_t total_steps = 0;

// Debouncing
static volatile uint32_t last_step_time_ms = 0;

// MAC address (cached)
static char device_mac[18] = {0};

/**
 * @brief ISR handler for step detection
 */
static void IRAM_ATTR step_isr_handler(void *arg)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    // Debounce
    if (now_ms - last_step_time_ms < DEBOUNCE_MS) {
        return;
    }
    last_step_time_ms = now_ms;

    // Increment total step counter
    total_steps++;

    // Add timestamp to buffer if not full
    if (step_buffer_size < MAX_BUFFERED_STEPS) {
        uint64_t timestamp_ms = esp_timer_get_time() / 1000; // Convert microseconds to milliseconds
        step_buffer[step_buffer_write_idx] = timestamp_ms;
        step_buffer_write_idx = (step_buffer_write_idx + 1) % MAX_BUFFERED_STEPS;
        step_buffer_size++;
    } else {
        // Buffer full - step lost
        ESP_EARLY_LOGW(TAG, "Step buffer full!");
    }
}

esp_err_t step_counter_init(void)
{
    ESP_LOGI(TAG, "Initializing step counter on GPIO %d", STEP_GPIO);

    // Get and cache MAC address
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(err));
        return err;
    }
    snprintf(device_mac, sizeof(device_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device MAC: %s", device_mac);

    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << STEP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE  // Trigger on both rising and falling edges
    };

    err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(err));
        return err;
    }

    // Install ISR service
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means service is already installed, which is fine
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(err));
        return err;
    }

    // Add ISR handler for the GPIO
    err = gpio_isr_handler_add(STEP_GPIO, step_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Step counter initialized successfully");
    return ESP_OK;
}

uint8_t step_counter_get_buffer_size(void)
{
    return step_buffer_size;
}

esp_err_t step_counter_get_mac_string(char *mac_str, size_t size)
{
    if (mac_str == NULL || size < 18) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(mac_str, device_mac, size - 1);
    mac_str[size - 1] = '\0';

    return ESP_OK;
}

esp_err_t step_counter_flush_one(void)
{
    // Check if buffer is empty
    if (step_buffer_size == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    // Check WebSocket connection
    if (!websocket_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get the oldest step from buffer
    uint64_t timestamp_ms = step_buffer[step_buffer_read_idx];

    // Build JSON message matching the format from the Arduino version
    // {"action":"sendStep","data":{"sent_at":1234567890.123,"deviceMAC":"XX:XX:XX:XX:XX:XX"}}
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON root");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "action", "sendStep");

    cJSON *data = cJSON_CreateObject();
    if (data == NULL) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to create JSON data object");
        return ESP_ERR_NO_MEM;
    }

    // Convert milliseconds to seconds with 3 decimal places
    double sent_at = (double)timestamp_ms / 1000.0;
    cJSON_AddNumberToObject(data, "sent_at", sent_at);
    cJSON_AddStringToObject(data, "deviceMAC", device_mac);

    cJSON_AddItemToObject(root, "data", data);

    // Generate JSON string
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sending step: %s", json_string);

    // Send via WebSocket
    esp_websocket_client_handle_t ws_client = websocket_client_get_handle();
    if (ws_client == NULL) {
        free(json_string);
        ESP_LOGE(TAG, "WebSocket client handle is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    int sent = esp_websocket_client_send_text(ws_client, json_string, strlen(json_string), portMAX_DELAY);
    free(json_string);

    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send step data");
        return ESP_FAIL;
    }

    // Successfully sent - remove from buffer
    step_buffer_read_idx = (step_buffer_read_idx + 1) % MAX_BUFFERED_STEPS;
    step_buffer_size--;

    ESP_LOGI(TAG, "Step sent successfully, %d steps remaining in buffer", step_buffer_size);

    return ESP_OK;
}

uint32_t step_counter_get_total_steps(void)
{
    return total_steps;
}
