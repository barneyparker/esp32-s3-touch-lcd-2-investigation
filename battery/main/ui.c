#include "ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include <string.h>

#define LCD_H_RES 240
#define LCD_V_RES 320
#define LVGL_TICK_MS 5

static const char *TAG = "ui";

static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_disp_drv_t *cached_disp_drv = NULL;
static lv_color_t *disp_buf1 = NULL;
static lv_obj_t *label_adc = NULL;
static lv_obj_t *label_voltage = NULL;
static lv_obj_t *label_percent = NULL;
static lv_obj_t *label_steps = NULL;
static lv_obj_t *label_buffer_count = NULL;
static lv_obj_t *label_wifi_status = NULL;
static lv_obj_t *label_ws_status = NULL;
static lv_obj_t *label_startup_status = NULL;
static lv_obj_t *startup_spinner = NULL;
static lv_obj_t *qr_code = NULL;
static lv_obj_t *main_container = NULL;
static SemaphoreHandle_t lvgl_api_mux = NULL;

static bool lvgl_lock(int timeout_ms)
{
  if (lvgl_api_mux == NULL)
    return false;
  const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTakeRecursive(lvgl_api_mux, timeout_ticks) == pdTRUE;
}

static void lvgl_unlock(void)
{
  xSemaphoreGiveRecursive(lvgl_api_mux);
}

bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
  lv_disp_flush_ready(&disp_drv);
  return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
  cached_disp_drv = drv;
  int offsetx1 = area->x1;
  int offsetx2 = area->x2;
  int offsety1 = area->y1;
  int offsety2 = area->y2;
  esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
  // lv_disp_flush_ready will be called by notify_lvgl_flush_ready when DMA completes
}

static void lv_tick_task(void *arg)
{
  (void)arg;
  TickType_t delay_ticks = pdMS_TO_TICKS(LVGL_TICK_MS);
  if (delay_ticks == 0)
    delay_ticks = 1;
  while (1)
  {
    lv_tick_inc(LVGL_TICK_MS);
    vTaskDelay(delay_ticks);
  }
}

static void lv_task(void *arg)
{
  (void)arg;
  TickType_t task_delay = pdMS_TO_TICKS(5);
  if (task_delay == 0)
    task_delay = 1;
  while (1)
  {
    if (lvgl_lock(500))
    {
      lv_task_handler();
      lvgl_unlock();
    }
    vTaskDelay(task_delay);
  }
}

void ui_init(esp_lcd_panel_handle_t lcd_panel)
{
  ESP_LOGI(TAG, "Initializing LVGL and UI");

  panel_handle = lcd_panel;

  // LVGL init
  lv_init();

  // Create recursive mutex to protect LVGL API calls
  lvgl_api_mux = xSemaphoreCreateRecursiveMutex();
  assert(lvgl_api_mux != NULL);

  // Allocate draw buffer (80 lines for better rendering with large fonts)
  disp_buf1 = heap_caps_malloc(LCD_H_RES * 80 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  assert(disp_buf1 != NULL);
  lv_disp_draw_buf_init(&draw_buf, disp_buf1, NULL, LCD_H_RES * 80);

  // Register display driver
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_H_RES;
  disp_drv.ver_res = LCD_V_RES;
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Create startup screen with spinner and status
  lv_obj_t *scr = lv_scr_act();

  // Spinner in center
  startup_spinner = lv_spinner_create(scr, 1000, 60);
  lv_obj_set_size(startup_spinner, 100, 100);
  lv_obj_align(startup_spinner, LV_ALIGN_CENTER, 0, -20);

  // Status label at bottom
  label_startup_status = lv_label_create(scr);
  lv_label_set_text(label_startup_status, "Starting up...");
  lv_obj_set_style_text_font(label_startup_status, &lv_font_montserrat_14, 0);
  lv_obj_align(label_startup_status, LV_ALIGN_BOTTOM_MID, 0, -20);

  // Create LVGL tasks AFTER UI elements are created
  xTaskCreate(lv_tick_task, "lv_tick", 2048, NULL, 5, NULL);
  xTaskCreate(lv_task, "lv_task", 4096, NULL, 5, NULL);

  ESP_LOGI(TAG, "UI initialized with startup screen");
}

void ui_update_startup_status(const char *status)
{
  if (!lvgl_lock(500))
    return;

  if (label_startup_status != NULL)
  {
    lv_label_set_text(label_startup_status, status);
    ESP_LOGI(TAG, "Startup: %s", status);
  }

  lvgl_unlock();
}

void ui_show_main_screen(void)
{
  if (!lvgl_lock(500))
    return;

  // Clear startup screen
  if (startup_spinner != NULL)
  {
    lv_obj_del(startup_spinner);
    startup_spinner = NULL;
  }
  if (label_startup_status != NULL)
  {
    lv_obj_del(label_startup_status);
    label_startup_status = NULL;
  }

  // Create main UI
  lv_obj_t *scr = lv_scr_act();
  main_container = lv_obj_create(scr);
  lv_obj_set_size(main_container, lv_pct(100), lv_pct(100));

  // Top left: Buffer count (unsent steps)
  label_buffer_count = lv_label_create(main_container);
  lv_label_set_text(label_buffer_count, "Q:0");
  lv_obj_set_style_text_font(label_buffer_count, &lv_font_montserrat_12, 0);
  lv_obj_align(label_buffer_count, LV_ALIGN_TOP_LEFT, 5, 5);

  // Top right: Battery percentage
  label_percent = lv_label_create(main_container);
  lv_label_set_text(label_percent, "100%");
  lv_obj_set_style_text_font(label_percent, &lv_font_montserrat_12, 0);
  lv_obj_align(label_percent, LV_ALIGN_TOP_RIGHT, -5, 5);

  // WiFi status icon (next to battery)
  label_wifi_status = lv_label_create(main_container);
  lv_label_set_text(label_wifi_status, "W:-");
  lv_obj_set_style_text_font(label_wifi_status, &lv_font_montserrat_12, 0);
  lv_obj_align(label_wifi_status, LV_ALIGN_TOP_RIGHT, -5, 20);

  // WebSocket status icon (next to WiFi)
  label_ws_status = lv_label_create(main_container);
  lv_label_set_text(label_ws_status, "S:-");
  lv_obj_set_style_text_font(label_ws_status, &lv_font_montserrat_12, 0);
  lv_obj_align(label_ws_status, LV_ALIGN_TOP_RIGHT, -5, 35);

  // Large step counter in center
  label_steps = lv_label_create(main_container);
  lv_label_set_text(label_steps, "0");
  lv_obj_set_style_text_font(label_steps, &lv_font_montserrat_48, 0);
  lv_obj_align(label_steps, LV_ALIGN_CENTER, 0, 0);

  lvgl_unlock();

  ESP_LOGI(TAG, "Main screen shown");
}

void ui_show_qr_code(const char *qr_data, const char *message)
{
  if (!lvgl_lock(500))
    return;

  // Clear spinner if it exists
  if (startup_spinner != NULL)
  {
    lv_obj_del(startup_spinner);
    startup_spinner = NULL;
  }

  // Delete existing QR code if present
  if (qr_code != NULL)
  {
    lv_obj_del(qr_code);
    qr_code = NULL;
  }

  // Create QR code widget - make it as large as possible while fitting on screen
  // Screen is 240x320, leave room for text at top and bottom
  lv_obj_t *scr = lv_scr_act();
  qr_code = lv_qrcode_create(scr, 200, lv_color_black(), lv_color_white());
  lv_qrcode_update(qr_code, qr_data, strlen(qr_data));
  lv_obj_align(qr_code, LV_ALIGN_CENTER, 0, -10);

  // Update status message
  if (label_startup_status != NULL)
  {
    lv_label_set_text(label_startup_status, message);
    lv_obj_align(label_startup_status, LV_ALIGN_BOTTOM_MID, 0, -20);
  }

  ESP_LOGI(TAG, "QR code displayed: %s", qr_data);
  lvgl_unlock();
}

void ui_update_battery(float voltage, int adc_raw, int pct_milli)
{
  if (!lvgl_lock(500))
    return;

  char buf[64];

  if (label_adc)
  {
    snprintf(buf, sizeof(buf), "ADC: %d", adc_raw);
    lv_label_set_text(label_adc, buf);
  }

  if (label_voltage)
  {
    int mv = (int)(voltage * 1000.0f + 0.5f);
    int whole = mv / 1000;
    int frac = mv % 1000;
    if (frac < 0)
      frac = -frac;
    snprintf(buf, sizeof(buf), "Volt: %d.%03d V", whole, frac);
    lv_label_set_text(label_voltage, buf);
  }

  if (label_percent)
  {
    int pct_whole = pct_milli / 10;
    int pct_tenth = pct_milli % 10;
    snprintf(buf, sizeof(buf), "Pct: %d.%d%%", pct_whole, pct_tenth);
    lv_label_set_text(label_percent, buf);
  }

  lvgl_unlock();
}

void ui_update_status(uint32_t step_count, uint8_t buffer_count, bool wifi_connected, bool ws_connected, int battery_pct)
{
  if (!lvgl_lock(500))
    return;

  char buf[32];

  // Update step count (center, large)
  if (label_steps)
  {
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)step_count);
    lv_label_set_text(label_steps, buf);
  }

  // Update buffer count (top left)
  if (label_buffer_count)
  {
    snprintf(buf, sizeof(buf), "Q:%u", buffer_count);
    lv_label_set_text(label_buffer_count, buf);
  }

  // Update battery percentage (top right)
  if (label_percent)
  {
    snprintf(buf, sizeof(buf), "%d%%", battery_pct);
    lv_label_set_text(label_percent, buf);
  }

  // Update WiFi status
  if (label_wifi_status)
  {
    lv_label_set_text(label_wifi_status, wifi_connected ? "W:OK" : "W:-");
  }

  // Update WebSocket status
  if (label_ws_status)
  {
    lv_label_set_text(label_ws_status, ws_connected ? "S:OK" : "S:-");
  }

  lvgl_unlock();
}
