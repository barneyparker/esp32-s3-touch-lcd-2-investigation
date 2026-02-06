#include "ota.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "ui.h"
#include "amazon_root_ca.h"
#include <string.h>

static const char *TAG = "OTA";
static const char *FIRMWARE_URL = "https://steps.barneyparker.com/firmware/step-counter.bin";
static const char *NVS_NAMESPACE = "ota";
static const char *NVS_ETAG_KEY = "etag";

static char current_etag[128] = {0};

esp_err_t ota_init(void)
{
    esp_err_t err;
    nvs_handle_t nvs_handle;

    // Open NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No stored ETag found");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Read ETag
    size_t required_size = sizeof(current_etag);
    err = nvs_get_str(nvs_handle, NVS_ETAG_KEY, current_etag, &required_size);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded firmware ETag: %s", current_etag);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No stored ETag found");
        err = ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to read ETag: %s", esp_err_to_name(err));
    }

    return err;
}

static esp_err_t ota_save_etag(const char *etag)
{
    esp_err_t err;
    nvs_handle_t nvs_handle;

    // Open NVS with read/write access
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Write ETag
    err = nvs_set_str(nvs_handle, NVS_ETAG_KEY, etag);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write ETag: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Commit
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved firmware ETag: %s", etag);
        strncpy(current_etag, etag, sizeof(current_etag) - 1);
    } else {
        ESP_LOGE(TAG, "Failed to commit ETag: %s", esp_err_to_name(err));
    }

    return err;
}

const char* ota_get_current_etag(void)
{
    return current_etag[0] != '\0' ? current_etag : NULL;
}

static char remote_etag[128] = {0};
static bool etag_found = false;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_HEADER:
            if (strcasecmp(evt->header_key, "ETag") == 0) {
                strncpy(remote_etag, evt->header_value, sizeof(remote_etag) - 1);
                etag_found = true;
                ESP_LOGI(TAG, "Remote ETag: %s", remote_etag);
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static esp_err_t ota_check_etag(char *out_etag, size_t etag_size)
{
    etag_found = false;
    remote_etag[0] = '\0';

    esp_http_client_config_t config = {
        .url = FIRMWARE_URL,
        .cert_pem = amazon_root_ca,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_HEAD,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP HEAD request failed: %s", esp_err_to_name(err));
        return err;
    }

    if (status_code == 404) {
        ESP_LOGW(TAG, "Firmware file not found (404)");
        return ESP_ERR_NOT_FOUND;
    }

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP HEAD returned status %d", status_code);
        return ESP_FAIL;
    }

    if (!etag_found || remote_etag[0] == '\0') {
        ESP_LOGW(TAG, "No ETag header in response");
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(out_etag, remote_etag, etag_size - 1);
    return ESP_OK;
}

static void ota_progress_callback(int image_size, int downloaded_bytes)
{
    static int last_percent = -1;
    int percent = (downloaded_bytes * 100) / image_size;

    if (percent != last_percent) {
        ESP_LOGI(TAG, "Download progress: %d%% (%d / %d bytes)", percent, downloaded_bytes, image_size);
        ui_update_ota_progress(percent);
        last_percent = percent;
    }
}

esp_err_t ota_check_and_update(void)
{
    ESP_LOGI(TAG, "Checking for firmware updates...");

    // Check remote ETag
    char new_etag[128] = {0};
    esp_err_t err = ota_check_etag(new_etag, sizeof(new_etag));

    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No firmware file available or no ETag");
        return ESP_OK; // Not an error, just no update available
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to check ETag");
        return err;
    }

    // Compare ETags
    if (current_etag[0] != '\0' && strcmp(new_etag, current_etag) == 0) {
        ESP_LOGI(TAG, "Firmware is up to date (ETag match)");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "New firmware available - downloading...");
    ui_show_ota_status(true);
    ui_update_ota_progress(0);

    // Configure OTA
    esp_http_client_config_t http_config = {
        .url = FIRMWARE_URL,
        .cert_pem = amazon_root_ca,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        ui_show_ota_status(false);
        return err;
    }

    int image_size = esp_https_ota_get_image_size(ota_handle);
    ESP_LOGI(TAG, "Firmware size: %d bytes", image_size);

    // Download with progress updates
    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        int downloaded = esp_https_ota_get_image_len_read(ota_handle);
        ota_progress_callback(image_size, downloaded);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA download failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        ui_show_ota_status(false);
        return err;
    }

    ui_update_ota_progress(100);
    ESP_LOGI(TAG, "Download complete, finishing OTA...");

    err = esp_https_ota_finish(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        ui_show_ota_status(false);
        return err;
    }

    // Save new ETag to NVS
    err = ota_save_etag(new_etag);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save ETag, but OTA succeeded");
    }

    ESP_LOGI(TAG, "OTA update successful! Rebooting in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;
}
