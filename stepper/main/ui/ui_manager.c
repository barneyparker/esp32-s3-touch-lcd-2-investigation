#include "ui_manager.h"
#include "ui_step_mode.h"
#include "display_driver.h"
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "ui_mgr";

// UI state
static lv_obj_t* main_screen = NULL;
static SemaphoreHandle_t ui_mutex = NULL;

esp_err_t ui_manager_init(void) {
    ESP_LOGI(TAG, "Initializing UI manager");

    // Create UI mutex for thread safety
    ui_mutex = xSemaphoreCreateMutex();
    if (!ui_mutex) {
        ESP_LOGE(TAG, "Failed to create UI mutex");
        return ESP_ERR_NO_MEM;
    }

    // Lock LVGL for setup
    display_driver_lock(1000);

    // Create main screen (dark background)
    main_screen = lv_obj_create(NULL);
    lv_obj_set_size(main_screen, 320, 240);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(main_screen, 0, 0);

    // Load step mode UI
    ESP_LOGI(TAG, "Creating step mode UI...");
    ui_step_mode_create();

    // Show the main screen
    ESP_LOGI(TAG, "Loading main screen...");
    lv_disp_load_scr(main_screen);

    // Force multiple display updates to ensure rendering
    ESP_LOGI(TAG, "Forcing display refresh...");
    lv_obj_invalidate(main_screen);
    lv_refr_now(lv_disp_get_default());
    lv_task_handler();  // Process pending tasks
    lv_refr_now(lv_disp_get_default());  // Force another refresh

    // Unlock LVGL
    display_driver_unlock();

    ESP_LOGI(TAG, "UI manager initialized - display should now show content");
    return ESP_OK;
}

void ui_manager_update(const app_state_t* state) {
    if (!state) {
        return;
    }

    xSemaphoreTake(ui_mutex, portMAX_DELAY);
    display_driver_lock(1000);

    // Update step count display
    ui_step_mode_update_count(state->step_count);

    // Update time display
    ui_step_mode_update_time(state);

    // Update WiFi status and SSID
    ui_step_mode_update_wifi(state->wifi_state, state->wifi_rssi);
    ui_step_mode_update_wifi_ssid(state->wifi_ssid);

    // Update WebSocket status
    ui_step_mode_update_ws(state->ws_state);

    // Update battery
    ui_step_mode_update_battery(state->battery_percent, state->battery_charging);

    // Update backlog
    ui_step_mode_update_backlog(state->backlog_size);

    display_driver_unlock();
    xSemaphoreGive(ui_mutex);
}

void ui_manager_show_step_mode(void) {
    if (main_screen) {
        display_driver_lock(1000);
        lv_disp_load_scr(main_screen);
        display_driver_unlock();
    }
}

void ui_manager_show_setup(void) {
    // TODO: Implement setup screen
    ESP_LOGI(TAG, "Setup screen requested");
}

void ui_manager_clear(void) {
    display_driver_lock(1000);
    if (main_screen) {
        lv_obj_clean(main_screen);
    }
    display_driver_unlock();
}
