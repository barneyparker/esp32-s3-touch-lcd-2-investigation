#include "ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"

#define LCD_H_RES 240
#define LCD_V_RES 320
#define LVGL_TICK_MS 5

static const char *TAG = "ui";

static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_disp_drv_t *cached_disp_drv = NULL;
static lv_obj_t *label_adc = NULL;
static lv_obj_t *label_voltage = NULL;
static lv_obj_t *label_percent = NULL;
static lv_obj_t *label_touch = NULL;
static lv_obj_t *label_steps = NULL;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_buf1 = NULL;
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

  // Create UI elements BEFORE starting tasks
  lv_obj_t *scr = lv_scr_act();
  lv_obj_t *cont = lv_obj_create(scr);
  lv_obj_set_size(cont, lv_pct(100), lv_pct(100));

  // Large step counter in center
  label_steps = lv_label_create(cont);
  lv_label_set_text(label_steps, "8008");
  lv_obj_set_style_text_font(label_steps, &lv_font_montserrat_48, 0);
  lv_obj_align(label_steps, LV_ALIGN_CENTER, 0, 0);

  // Small battery info at top
  label_percent = lv_label_create(cont);
  lv_label_set_text(label_percent, "Pct: 0.0%");
  lv_obj_align(label_percent, LV_ALIGN_TOP_MID, 0, 10);

  label_voltage = lv_label_create(cont);
  lv_label_set_text(label_voltage, "Volt: 0.000 V");
  lv_obj_set_style_text_font(label_voltage, &lv_font_montserrat_12, 0);
  lv_obj_align(label_voltage, LV_ALIGN_TOP_MID, 0, 35);

  label_adc = lv_label_create(cont);
  lv_label_set_text(label_adc, "ADC: 0");
  lv_obj_set_style_text_font(label_adc, &lv_font_montserrat_12, 0);
  lv_obj_align(label_adc, LV_ALIGN_BOTTOM_MID, 0, -10);

  label_touch = lv_label_create(cont);
  lv_label_set_text(label_touch, "Touch: x=0, y=0");
  lv_obj_set_style_text_font(label_touch, &lv_font_montserrat_12, 0);
  lv_obj_align(label_touch, LV_ALIGN_BOTTOM_LEFT, 10, -10);

  // Create LVGL tasks AFTER UI elements are created
  xTaskCreate(lv_tick_task, "lv_tick", 2048, NULL, 5, NULL);
  xTaskCreate(lv_task, "lv_task", 4096, NULL, 5, NULL);

  ESP_LOGI(TAG, "UI initialized");
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
