#include "ota_updater.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
#include "app_state.h"
#include "storage_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include <string.h>

static const char *TAG = "ota_updater";

typedef struct {
    esp_http_client_handle_t http_client;
    esp_ota_handle_t ota_handle;
    const esp_partition_t *ota_partition;
    int received_bytes;
    int total_bytes;
} ota_update_state_t;

static ota_update_state_t update_state = {0};
static bool ota_in_progress = false;

// HTTP event handler for OTA download
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP connection established");
            break;

        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP headers sent");
            break;

        case HTTP_EVENT_ON_HEADER:
            if (strcmp(evt->header_key, "Content-Length") == 0) {
                update_state.total_bytes = atoi(evt->header_value);
                ESP_LOGI(TAG, "Firmware size: %d bytes", update_state.total_bytes);
                app_state_set_ota(OTA_STATE_DOWNLOADING, 0);
            }
            break;

        case HTTP_EVENT_ON_DATA:
            if (update_state.ota_handle) {
                esp_err_t ret = esp_ota_write(update_state.ota_handle, evt->data, evt->data_len);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Error writing OTA data: %s", esp_err_to_name(ret));
                    return ESP_FAIL;
                }

                update_state.received_bytes += evt->data_len;
                uint8_t progress = (update_state.received_bytes * 100) / update_state.total_bytes;
                ESP_LOGD(TAG, "OTA download progress: %d%%", progress);
                app_state_set_ota(OTA_STATE_DOWNLOADING, progress);
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP download completed");
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP disconnected");
            break;

        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP error occurred");
            break;

        default:
            break;
    }

    return ESP_OK;
}

esp_err_t ota_updater_init(void)
{
    ESP_LOGI(TAG, "OTA updater initialized");
    return ESP_OK;
}

esp_err_t ota_updater_check_update(const char *url, char *out_version, size_t version_len)
{
    if (!url || !out_version) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Checking for firmware updates at %s", url);
    app_state_set_ota(OTA_STATE_CHECKING, 0);

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .timeout_ms = 30000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        app_state_set_ota(OTA_STATE_ERROR, 0);
        return ESP_FAIL;
    }

    esp_err_t ret = esp_http_client_perform(client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(ret));
        app_state_set_ota(OTA_STATE_ERROR, 0);
        esp_http_client_cleanup(client);
        return ret;
    }

    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP error: %d", status_code);
        app_state_set_ota(OTA_STATE_ERROR, 0);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    // Parse version from response headers or body
    // For now, store the ETag as version identifier
    char *etag = NULL;
    esp_err_t header_ret = esp_http_client_get_header(client, "ETag", &etag);
    if (header_ret == ESP_OK && etag) {
        strncpy(out_version, etag, version_len - 1);
        ESP_LOGI(TAG, "Latest version: %s", out_version);
    }

    esp_http_client_cleanup(client);
    return ESP_OK;
}

esp_err_t ota_updater_download_update(const char *url)
{
    if (!url || ota_in_progress) {
        return ESP_ERR_INVALID_STATE;
    }

    ota_in_progress = true;
    memset(&update_state, 0, sizeof(update_state));

    ESP_LOGI(TAG, "Starting firmware download from %s", url);
    app_state_set_ota(OTA_STATE_DOWNLOADING, 0);

    // Get the next OTA partition
    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (!ota_partition) {
        ESP_LOGE(TAG, "Failed to get OTA partition");
        app_state_set_ota(OTA_STATE_ERROR, 0);
        ota_in_progress = false;
        return ESP_FAIL;
    }

    update_state.ota_partition = ota_partition;

    // Begin OTA write
    esp_err_t ret = esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &update_state.ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to begin OTA: %s", esp_err_to_name(ret));
        app_state_set_ota(OTA_STATE_ERROR, 0);
        ota_in_progress = false;
        return ret;
    }

    // Download and write firmware
    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .timeout_ms = 30000,
    };

    update_state.http_client = esp_http_client_init(&config);
    if (!update_state.http_client) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        esp_ota_end(update_state.ota_handle);
        app_state_set_ota(OTA_STATE_ERROR, 0);
        ota_in_progress = false;
        return ESP_FAIL;
    }

    ret = esp_http_client_perform(update_state.http_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP download failed: %s", esp_err_to_name(ret));
        esp_ota_end(update_state.ota_handle);
        esp_http_client_cleanup(update_state.http_client);
        app_state_set_ota(OTA_STATE_ERROR, 0);
        ota_in_progress = false;
        return ret;
    }

    esp_http_client_cleanup(update_state.http_client);

    // End OTA write
    ret = esp_ota_end(update_state.ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(ret));
        app_state_set_ota(OTA_STATE_ERROR, 0);
        ota_in_progress = false;
        return ret;
    }

    ESP_LOGI(TAG, "Firmware downloaded successfully (%d bytes)", update_state.received_bytes);
    app_state_set_ota(OTA_STATE_COMPLETE, 100);
    return ESP_OK;
}

esp_err_t ota_updater_install_update(void)
{
    if (!update_state.ota_partition) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Installing firmware from partition %d", update_state.ota_partition->subtype);
    app_state_set_ota(OTA_STATE_INSTALLING, 50);

    esp_err_t ret = esp_ota_set_boot_partition(update_state.ota_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(ret));
        app_state_set_ota(OTA_STATE_ERROR, 0);
        return ret;
    }

    ESP_LOGI(TAG, "Firmware will be installed on next reboot");
    app_state_set_ota(OTA_STATE_COMPLETE, 100);

    ota_in_progress = false;
    return ESP_OK;
}

bool ota_updater_is_in_progress(void)
{
    return ota_in_progress;
}
