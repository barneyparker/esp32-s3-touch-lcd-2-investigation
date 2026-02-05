#include "display_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst816s.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"

static const char* TAG = "display_drv";

/* Pin Definitions */
#define LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)
#define LCD_H_RES 240
#define LCD_V_RES 320
#define LCD_PIN_DC 42
#define LCD_PIN_RST -1
#define LCD_PIN_CS 45

#define SPI_PIN_SCLK 39
#define SPI_PIN_MOSI 38
#define SPI_PIN_MISO 40
#define SPI_HOST SPI2_HOST

#define I2C_PORT_NUM 0
#define I2C_PIN_SDA 48
#define I2C_PIN_SCL 47

#define BK_LIGHT_PIN 1
#define LVGL_TICK_MS 5

/* Global state */
static SemaphoreHandle_t lvgl_api_mux = NULL;
static lv_disp_drv_t disp_drv = {0};
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static lv_color_t* disp_buf1 = NULL;
static lv_disp_draw_buf_t draw_buf = {0};
static i2c_master_bus_handle_t i2c_bus = NULL;
static esp_lcd_touch_handle_t tp = NULL;
static uint8_t brightness_percent = 100;

/* Display refresh callback */
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                     esp_lcd_panel_io_event_data_t* edata,
                                     void* user_ctx) {
    lv_disp_flush_ready(&disp_drv);
    return false;
}

/* LVGL display flush callback */
static void lvgl_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map) {
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

/* Touch input callback */
static lv_indev_drv_t indev_drv = {0};
static bool touch_pressed = false;

static void lvgl_touch_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    if (tp == NULL) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    esp_lcd_touch_point_data_t points[1];
    uint8_t point_cnt = 0;

    esp_lcd_touch_read_data(tp);
    esp_err_t err = esp_lcd_touch_get_data(tp, points, &point_cnt, 1);

    if (err == ESP_OK && point_cnt > 0) {
        data->point.x = points[0].x;
        data->point.y = points[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
        touch_pressed = true;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        touch_pressed = false;
    }
}

/* LVGL handler task - processes LVGL timers and updates */
static void lv_task(void* arg) {
    (void)arg;
    TickType_t task_delay = pdMS_TO_TICKS(5);
    if (task_delay == 0) task_delay = 1;

    while (1) {
        if (display_driver_lock(500)) {
            lv_task_handler();
            display_driver_unlock();
        }
        vTaskDelay(task_delay);
    }
}

/* Initialize I2C for touch controller */
static esp_err_t i2c_init(void) {
    i2c_master_bus_config_t mb_cfg = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = I2C_PIN_SDA,
        .scl_io_num = I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t r = i2c_new_master_bus(&mb_cfg, &i2c_bus);
    if (r == ESP_OK) {
        ESP_LOGI(TAG, "I2C master bus created");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "I2C master bus creation failed: %s", esp_err_to_name(r));
    i2c_bus = NULL;
    return r;
}

/* Initialize touch controller */
static esp_err_t touch_init(void) {
    if (i2c_bus == NULL) {
        ESP_LOGW(TAG, "I2C bus not initialized, skipping touch");
        return ESP_ERR_INVALID_STATE;
    }

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    tp_io_config.scl_speed_hz = 400000;

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_err_t err = esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create touch IO: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_V_RES,
        .y_max = LCD_H_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    err = esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create touch driver: %s", esp_err_to_name(err));
        return err;
    }

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "Touch controller initialized");
    return ESP_OK;
}

esp_err_t display_driver_init(void) {
    ESP_LOGI(TAG, "Initializing display driver");

    // Initialize LVGL
    lv_init();

    // Create LVGL mutex
    lvgl_api_mux = xSemaphoreCreateRecursiveMutex();
    if (lvgl_api_mux == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize backlight PWM (LEDC will configure the GPIO automatically)
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 10000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BK_LIGHT_PIN,
        .duty = 512,  // Start at 50% like working demo
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    brightness_percent = 50;
    ESP_LOGI(TAG, "Backlight PWM initialized at 50% brightness");

    // Initialize I2C
    ESP_ERROR_CHECK(i2c_init());

    // Initialize SPI for LCD
    spi_bus_config_t buscfg = {
        .sclk_io_num = SPI_PIN_SCLK,
        .mosi_io_num = SPI_PIN_MOSI,
        .miso_io_num = SPI_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(lv_color_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus initialized");

    // Initialize LCD panel IO
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
    ESP_LOGI(TAG, "LCD panel IO initialized");

    // Initialize LCD panel
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
    ESP_LOGI(TAG, "LCD panel initialized");

    // Initialize LVGL display buffer - MUST use DMA-capable memory (not PSRAM)
    // Use smaller buffer (40 lines) to fit in internal RAM
    disp_buf1 = heap_caps_malloc(LCD_H_RES * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (disp_buf1 == NULL) {
        ESP_LOGE(TAG, "Failed to allocate LVGL display buffer");
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&draw_buf, disp_buf1, NULL, LCD_H_RES * 40);

    // Initialize LVGL display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    ESP_LOGI(TAG, "LVGL display driver registered");

    // Start LVGL handler task (tick handled by LV_TICK_CUSTOM via esp_timer)
    xTaskCreate(lv_task, "lv_task", 4096, NULL, 5, NULL);

    // Initialize touch
    touch_init();

    ESP_LOGI(TAG, "Display driver initialization complete");
    return ESP_OK;
}

void display_driver_set_brightness(uint8_t percent) {
    if (percent > 100) {
        percent = 100;
    }
    brightness_percent = percent;

    // Map 0-100 to 0-(2^10-1) for PWM duty (10-bit resolution)
    const uint32_t max_duty = (1 << 10) - 1;
    uint32_t duty = (percent * max_duty) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    ESP_LOGI(TAG, "Brightness set to %d%% (duty=%d/%d)", percent, duty, max_duty);
}

uint8_t display_driver_get_brightness(void) {
    return brightness_percent;
}

uint16_t display_driver_get_width(void) {
    return LCD_H_RES;
}

uint16_t display_driver_get_height(void) {
    return LCD_V_RES;
}

bool display_driver_lock(int timeout_ms) {
    if (lvgl_api_mux == NULL) {
        return false;
    }
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_api_mux, timeout_ticks) == pdTRUE;
}

void display_driver_unlock(void) {
    if (lvgl_api_mux != NULL) {
        xSemaphoreGiveRecursive(lvgl_api_mux);
    }
}
