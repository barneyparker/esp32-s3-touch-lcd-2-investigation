#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_log.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

/* Display / LVGL includes (merged from demo) */
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst816s.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

/* ---------- Battery ---------- */
#define BAT_ADC_CHANNEL ADC_CHANNEL_4 // Change to the ADC channel wired to battery divider
#define BAT_ADC_ATTEN ADC_ATTEN_DB_12
#define BAT_ADC_UNIT ADC_UNIT_1
#define BAT_ADC_SAMPLES 8
#define BAT_VOLTAGE_DIVIDER_FACTOR 3.0f // If a divider is used, set factor = (R1+R2)/R2
#define BAT_V_EMPTY_MV 3000             // Battery empty voltage (mV)
#define BAT_V_FULL_MV 4200              // Battery full voltage (mV)

/* ---------- Display + LVGL ---------- */
#define LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)
#define LCD_H_RES 240
#define LCD_V_RES 320
#define LCD_PIN_DC 42
#define LCD_PIN_RST -1
#define LCD_PIN_CS 45

/* ---------- SPI ---------- */
#define SPI_PIN_SCLK 39
#define SPI_PIN_MOSI 38
#define SPI_PIN_MISO 40
#define SPI_HOST SPI2_HOST

/* ---------- I2C ---------- */
#define I2C_PIN_SDA 48
#define I2C_PIN_SCL 47

/* ---------- Backlight ---------- */
#define BK_LIGHT_PIN 1

#define LVGL_TICK_MS 5

static const char *TAG = "battery_report";
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_chan_handle = NULL;
static bool do_calibration = false;

static void battery_init(void)
{
  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = BAT_ADC_UNIT,
  };
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

  adc_oneshot_chan_cfg_t config = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = BAT_ADC_ATTEN,
  };
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BAT_ADC_CHANNEL, &config));

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  {
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = BAT_ADC_UNIT,
        .chan = BAT_ADC_CHANNEL,
        .atten = BAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_chan_handle) == ESP_OK)
    {
      do_calibration = true;
      ESP_LOGI(TAG, "ADC calibration: curve fitting enabled");
      return;
    }
  }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  {
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = BAT_ADC_UNIT,
        .atten = BAT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_chan_handle) == ESP_OK)
    {
      do_calibration = true;
      ESP_LOGI(TAG, "ADC calibration: line fitting enabled");
      return;
    }
  }
#endif

  do_calibration = false;
  ESP_LOGW(TAG, "ADC calibration: not available, using raw estimate");
}

static void read_battery(float *voltage_v, int *adc_raw_avg, int samples)
{
  long sum_raw = 0;
  for (int i = 0; i < samples; ++i)
  {
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BAT_ADC_CHANNEL, &raw));
    sum_raw += raw;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  int avg_raw = (int)(sum_raw / samples);
  *adc_raw_avg = avg_raw;

  if (do_calibration)
  {
    int voltage_mv = 0;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan_handle, avg_raw, &voltage_mv));
    float bat_v = (voltage_mv * BAT_VOLTAGE_DIVIDER_FACTOR) / 1000.0f;
    *voltage_v = bat_v;
  }
  else
  {
    // Fallback: estimate assuming 12-bit ADC and 3.3V reference
    float v = (avg_raw * (3.3f / 4095.0f)) * BAT_VOLTAGE_DIVIDER_FACTOR;
    *voltage_v = v;
  }
}

static int estimate_percentage_milli(float mv)
{
  int min = BAT_V_EMPTY_MV;
  int max = BAT_V_FULL_MV;
  int mv_i = (int)(mv * 1000.0f);
  if (mv_i <= min)
    return 0;
  if (mv_i >= max)
    return 1000;
  float p = (float)(mv_i - min) / (float)(max - min);
  return (int)(p * 1000.0f);
}

/* ---------- Integrated display + LVGL (based on 04_lvgl_battery) ---------- */

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static lv_disp_drv_t disp_drv;
static lv_obj_t *label_adc = NULL;
static lv_obj_t *label_voltage = NULL;
static lv_obj_t *label_percent = NULL;
static lv_obj_t *label_touch = NULL; // New label for touch coordinates
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_buf1 = NULL;
static SemaphoreHandle_t lvgl_api_mux = NULL;
static esp_lcd_touch_handle_t tp; // Touch handle

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

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
  lv_disp_flush_ready(&disp_drv);
  return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
  int offsetx1 = area->x1;
  int offsetx2 = area->x2;
  int offsety1 = area->y1;
  int offsety2 = area->y2;
  esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

static void lv_tick_task(void *arg)
{
  (void)arg;
  TickType_t delay_ticks = pdMS_TO_TICKS(LVGL_TICK_MS);
  if (delay_ticks == 0)
    delay_ticks = 1; // ensure at least one tick to avoid busy-loop when ms < tick period
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
    task_delay = 1; // ensure at least one tick
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

static void display_init(void)
{
  // backlight PWM
  ledc_timer_config_t ledc_timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_10_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = 10000,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&ledc_timer);
  ledc_channel_config_t ledc_channel = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .timer_sel = LEDC_TIMER_0,
      .intr_type = LEDC_INTR_DISABLE,
      .gpio_num = BK_LIGHT_PIN,
      .duty = 1024 / 2,
      .hpoint = 0,
  };
  ledc_channel_config(&ledc_channel);

  // LVGL init
  lv_init();

  /* Create recursive mutex to protect LVGL API calls from multiple tasks */
  lvgl_api_mux = xSemaphoreCreateRecursiveMutex();
  assert(lvgl_api_mux != NULL);

  xTaskCreate(lv_tick_task, "lv_tick", 2048, NULL, 5, NULL);
  xTaskCreate(lv_task, "lv_task", 4096, NULL, 5, NULL);

  // SPI bus (use spi_bus_initialize)
  spi_bus_config_t buscfg = {
      .sclk_io_num = SPI_PIN_SCLK,
      .mosi_io_num = SPI_PIN_MOSI,
      .miso_io_num = SPI_PIN_MISO,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = LCD_H_RES * 40 * sizeof(lv_color_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_config = {
      .dc_gpio_num = LCD_PIN_DC,
      .cs_gpio_num = LCD_PIN_CS,
      .pclk_hz = LCD_PIXEL_CLOCK_HZ,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .spi_mode = 0,
      .trans_queue_depth = 10,
      .on_color_trans_done = notify_lvgl_flush_ready,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST, &io_config, &io_handle));

  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = LCD_PIN_RST,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));

  // draw buffer
  disp_buf1 = heap_caps_malloc(LCD_H_RES * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  assert(disp_buf1 != NULL);
  lv_disp_draw_buf_init(&draw_buf, disp_buf1, NULL, LCD_H_RES * 40);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_H_RES;
  disp_drv.ver_res = LCD_V_RES;
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // UI
  lv_obj_t *scr = lv_scr_act();
  lv_obj_t *cont = lv_obj_create(scr);
  lv_obj_set_size(cont, lv_pct(100), lv_pct(100));

  label_adc = lv_label_create(cont);
  lv_label_set_text(label_adc, "ADC: 0");
  lv_obj_align(label_adc, LV_ALIGN_TOP_MID, 0, 10);

  label_voltage = lv_label_create(cont);
  lv_label_set_text(label_voltage, "Volt: 0.000 V");
  lv_obj_align(label_voltage, LV_ALIGN_TOP_MID, 0, 40);

  label_percent = lv_label_create(cont);
  lv_label_set_text(label_percent, "Pct: 0.0%");
  lv_obj_align(label_percent, LV_ALIGN_TOP_MID, 0, 70);

  label_touch = lv_label_create(cont);
  lv_label_set_text(label_touch, "Touch: x=0, y=0");
  lv_obj_align(label_touch, LV_ALIGN_TOP_LEFT, 10, 10);
}

static void display_update(float voltage, int adc_raw, int pct_milli)
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
    // pct_milli is thousandths (e.g., 952 -> 95.2%)
    int pct_whole = pct_milli / 10; // integer percent
    int pct_tenth = pct_milli % 10; // one decimal place
    snprintf(buf, sizeof(buf), "Pct: %d.%d%%", pct_whole, pct_tenth);
    lv_label_set_text(label_percent, buf);
  }

  lvgl_unlock();
  /* Let LVGL flush in its task */
}

void app_main(void)
{
  battery_init();
  ESP_LOGI(TAG, "checkpoint: after battery_init");

  // initialize and enable display
  display_init();
  ESP_LOGI(TAG, "checkpoint: after display_init");

  // initialize touch
  esp_lcd_touch_config_t tp_config = {
      .x_max = LCD_H_RES,
      .y_max = LCD_V_RES,
      .rst_gpio_num = -1,
      .int_gpio_num = -1,
      .levels = {
          .reset = 0,
          .interrupt = 0,
      },
  };
  ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(io_handle, &tp_config, &tp));

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  while (1)
  {
    float voltage = 0.0f;
    int adc_raw = 0;
    read_battery(&voltage, &adc_raw, BAT_ADC_SAMPLES);

    int pct_milli = estimate_percentage_milli(voltage);

    ESP_LOGI(TAG, "ADC raw: %d, Voltage: %.3f V, Percent: %.1f%%, calibration: %s, free heap: %u",
             adc_raw,
             voltage,
             pct_milli / 10.0f,
             do_calibration ? "yes" : "no",
             esp_get_free_heap_size());

    // update display with latest values
    display_update(voltage, adc_raw, pct_milli);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
