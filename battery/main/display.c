#include "display.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

/* Display configuration */
#define LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)
#define LCD_H_RES 240
#define LCD_V_RES 320
#define LCD_PIN_DC 42
#define LCD_PIN_RST -1
#define LCD_PIN_CS 45

/* SPI configuration */
#define SPI_PIN_SCLK 39
#define SPI_PIN_MOSI 38
#define SPI_PIN_MISO 40
#define SPI_HOST SPI2_HOST

/* Backlight configuration */
#define BK_LIGHT_PIN 1

static const char *TAG = "display";

esp_lcd_panel_handle_t display_init(esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done)
{
  ESP_LOGI(TAG, "Initializing display hardware");

  // Backlight PWM
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

  // SPI bus
  spi_bus_config_t buscfg = {
      .sclk_io_num = SPI_PIN_SCLK,
      .mosi_io_num = SPI_PIN_MOSI,
      .miso_io_num = SPI_PIN_MISO,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

  // LCD panel IO
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_config = {
      .dc_gpio_num = LCD_PIN_DC,
      .cs_gpio_num = LCD_PIN_CS,
      .pclk_hz = LCD_PIXEL_CLOCK_HZ,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .spi_mode = 0,
      .trans_queue_depth = 10,
      .on_color_trans_done = on_color_trans_done,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST, &io_config, &io_handle));

  // LCD panel
  esp_lcd_panel_handle_t panel_handle = NULL;
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

  ESP_LOGI(TAG, "Display hardware initialized");
  return panel_handle;
}

void display_set_backlight(uint8_t brightness)
{
  if (brightness > 100) {
    brightness = 100;
  }
  // Convert 0-100 to 0-1023 (10-bit PWM)
  uint32_t duty = (brightness * 1023) / 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void display_backlight_on(void)
{
  display_set_backlight(50);  // 50% brightness
}

void display_backlight_off(void)
{
  display_set_backlight(0);
}
