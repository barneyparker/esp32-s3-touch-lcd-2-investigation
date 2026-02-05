#include "ui_step_mode.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>
#include <time.h>

static const char* TAG = "ui_step";

// UI object references
static lv_obj_t* label_count = NULL;      // Large step count
static lv_obj_t* label_time = NULL;       // Current time
static lv_obj_t* label_backlog = NULL;    // Backlog counter
static lv_obj_t* label_battery = NULL;    // Battery level
static lv_obj_t* label_wifi = NULL;       // WiFi status
static lv_obj_t* label_wifi_ssid = NULL;  // WiFi network name
static lv_obj_t* label_ws = NULL;         // WebSocket status

/**
 * @brief Create large step count display (top center)
 */
static void create_step_display(lv_obj_t* parent) {
    // Create large step count label
    label_count = lv_label_create(parent);
    lv_label_set_text(label_count, "00000");
    lv_obj_set_align(label_count, LV_ALIGN_CENTER);
    lv_obj_set_y(label_count, -40);

    // Use large 48pt font for step count
    lv_obj_set_style_text_font(label_count, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(label_count, lv_color_hex(0xFFFFFF), 0);

    ESP_LOGI(TAG, "Step count label created at center with 48pt font");
}

/**
 * @brief Create time display (below step count)
 */
static void create_time_display(lv_obj_t* parent) {
    label_time = lv_label_create(parent);
    lv_label_set_text(label_time, "00:00");
    lv_obj_set_align(label_time, LV_ALIGN_CENTER);
    lv_obj_set_y(label_time, 30);

    // Use 20pt font for time
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label_time, lv_color_hex(0xCCCCCC), 0);
}

/**
 * @brief Create status indicators (bottom area)
 */
static void create_status_area(lv_obj_t* parent) {
    // WiFi status
    label_wifi = lv_label_create(parent);
    lv_label_set_text(label_wifi, "📶");
    lv_obj_set_align(label_wifi, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_pos(label_wifi, 10, -10);
    lv_obj_set_style_text_font(label_wifi, &lv_font_montserrat_14, 0);

    // WiFi SSID - display network name below the indicator
    label_wifi_ssid = lv_label_create(parent);
    lv_label_set_text(label_wifi_ssid, "");
    lv_obj_set_align(label_wifi_ssid, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_pos(label_wifi_ssid, 10, -30);
    lv_obj_set_style_text_font(label_wifi_ssid, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_wifi_ssid, lv_color_hex(0x99CCFF), 0);

    // WebSocket status
    label_ws = lv_label_create(parent);
    lv_label_set_text(label_ws, "◯");
    lv_obj_set_align(label_ws, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_pos(label_ws, 50, -10);
    lv_obj_set_style_text_font(label_ws, &lv_font_montserrat_14, 0);

    // Battery
    label_battery = lv_label_create(parent);
    lv_label_set_text(label_battery, "🔋 85%");
    lv_obj_set_align(label_battery, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_set_pos(label_battery, -10, -10);
    lv_obj_set_style_text_font(label_battery, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_battery, lv_color_hex(0x88FF88), 0);
}

/**
 * @brief Create backlog counter (upper right)
 */
static void create_backlog_display(lv_obj_t* parent) {
    label_backlog = lv_label_create(parent);
    lv_label_set_text(label_backlog, "↗ 0");
    lv_obj_set_align(label_backlog, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_pos(label_backlog, -10, 20);

    lv_obj_set_style_text_font(label_backlog, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label_backlog, lv_color_hex(0xFFAA00), 0);
}

void ui_step_mode_create(void) {
    lv_obj_t* screen = lv_scr_act();

    // Set dark background
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    ESP_LOGI(TAG, "Screen background set to dark color");

    // Create all UI elements
    ESP_LOGI(TAG, "Creating step display...");
    create_step_display(screen);
    ESP_LOGI(TAG, "Creating time display...");
    create_time_display(screen);
    ESP_LOGI(TAG, "Creating status area...");
    create_status_area(screen);
    ESP_LOGI(TAG, "Creating backlog display...");
    create_backlog_display(screen);

    // Make screen visible
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Step mode UI created with all elements");
}

void ui_step_mode_update_count(uint32_t count) {
    if (!label_count) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%05lu", count);
    lv_label_set_text(label_count, buf);
}

void ui_step_mode_update_time(const app_state_t* state) {
    if (!label_time || !state) return;

    char buf[32];
    time_t current_time = state->current_time;
    struct tm *timeinfo = localtime(&current_time);
    if (!timeinfo) {
        lv_label_set_text(label_time, "--:--");
        return;
    }
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
    lv_label_set_text(label_time, buf);
}

void ui_step_mode_update_wifi(wifi_state_t state, int8_t rssi) {
    if (!label_wifi) return;

    const char* icon = "❌";
    uint32_t color = 0xFF4444;  // Red

    switch (state) {
        case WIFI_STATE_CONNECTED:
            if (rssi > -50) {
                icon = "📶";  // Strong
                color = 0x44FF44;  // Green
            } else if (rssi > -70) {
                icon = "📶";  // Weak
                color = 0xFFFF44;  // Yellow
            } else {
                icon = "📶";  // Very weak
                color = 0xFFAA44;  // Orange
            }
            break;
        case WIFI_STATE_CONNECTING:
            icon = "◐";  // Connecting
            color = 0xFFCC00;  // Yellow
            break;
        case WIFI_STATE_DISCONNECTED:
        default:
            icon = "❌";  // Disconnected
            color = 0xFF4444;  // Red
            break;
    }

    lv_label_set_text(label_wifi, icon);
    lv_obj_set_style_text_color(label_wifi, lv_color_hex(color), 0);
}

void ui_step_mode_update_ws(ws_state_t state) {
    if (!label_ws) return;

    const char* icon = "◯";
    uint32_t color = 0xFF4444;  // Red

    switch (state) {
        case WS_STATE_CONNECTED:
            icon = "◉";  // Connected
            color = 0x44FF44;  // Green
            break;
        case WS_STATE_CONNECTING:
            icon = "◐";  // Connecting
            color = 0xFFCC00;  // Yellow
            break;
        case WS_STATE_DISCONNECTED:
        default:
            icon = "◯";  // Disconnected
            color = 0xFF4444;  // Red
            break;
    }

    lv_label_set_text(label_ws, icon);
    lv_obj_set_style_text_color(label_ws, lv_color_hex(color), 0);
}

void ui_step_mode_update_battery(uint8_t level, bool charging) {
    if (!label_battery) return;

    const char* icon = "🔋";
    uint32_t color = 0x88FF88;  // Green by default

    if (charging) {
        icon = "⚡";
        color = 0xFFFF44;  // Yellow when charging
    } else if (level < 20) {
        color = 0xFF4444;  // Red when low
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d%%", icon, level);
    lv_label_set_text(label_battery, buf);
    lv_obj_set_style_text_color(label_battery, lv_color_hex(color), 0);
}

void ui_step_mode_update_backlog(uint32_t backlog_size) {
    if (!label_backlog) return;

    char buf[32];
    snprintf(buf, sizeof(buf), "↗ %lu", backlog_size);
    lv_label_set_text(label_backlog, buf);

    // Change color based on backlog size
    uint32_t color = 0x88FF88;  // Green if empty
    if (backlog_size > 100) {
        color = 0xFF4444;  // Red if too large
    } else if (backlog_size > 10) {
        color = 0xFFAA00;  // Orange if moderate
    }
    lv_obj_set_style_text_color(label_backlog, lv_color_hex(color), 0);
}

void ui_step_mode_update_wifi_ssid(const char* ssid) {
    if (!label_wifi_ssid || !ssid) return;

    char buf[40];
    snprintf(buf, sizeof(buf), "%.30s", ssid);
    lv_label_set_text(label_wifi_ssid, buf);
}
