/*
 * ⚠️  CRITICAL PRODUCTION ISSUE - MUST FIX BEFORE RELEASE ⚠️
 *
 * This file currently has a STUB implementation of battery monitoring.
 * The actual ADC continuous mode initialization was removed because it was
 * causing crashes on ESP32-S3:
 *   - E (855) gdma: gdma_disconnect(347): invalid argument
 *   - E (856) adc_share_hw_ctrl: adc_apb_periph_free called, but `s_adc_digi_ctrlr_cnt == 0`
 *
 * Currently battery_level is hardcoded to 75% and does not reflect actual device voltage.
 *
 * REQUIRED FOR PRODUCTION:
 * 1. Properly implement ADC continuous sampling with correct GDMA configuration
 * 2. Test with real battery voltage readings (3.0V to 4.2V range)
 * 3. Verify charging detection on GPIO 41
 * 4. Ensure no GDMA/ADC control errors occur during init and sampling
 *
 * Related files:
 * - main/drivers/battery_monitor.h (battery_monitor_get_level, battery_is_charging)
 * - main/ui/ui_step_mode.c (battery display integration)
 * - sdkconfig (ADC driver configuration)
 *
 * See: https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/peripherals/adc.html
 */

#include "battery_monitor.h"
#include "app_state.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char* TAG = "battery_mon";

// ADC Configuration
#define BATTERY_ADC_UNIT      ADC_UNIT_1
#define BATTERY_ADC_CHANNEL   ADC_CHANNEL_4  // GPIO 4 on ESP32-S3 (matches working demo)
#define BATTERY_ADC_ATTEN     ADC_ATTEN_DB_12 // Full range: 0-3.3V with 12dB attenuation
#define BATTERY_ADC_RES       ADC_BITWIDTH_12

// Battery voltage thresholds (in mV)
#define BATTERY_MIN_MV        3000  // 0% at 3.0V
#define BATTERY_MAX_MV        4200  // 100% at 4.2V (lithium)
#define BATTERY_CHARGE_MV     4100  // Charging threshold

// Charging detect GPIO (optional, can be GPIO 41)
#define BATTERY_CHARGE_GPIO   GPIO_NUM_41

// ADC calibration
static adc_cali_handle_t adc_cali_handle = NULL;
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static bool do_calibration = false;
static uint8_t battery_level = 50;
static bool battery_charging = false;

// If the hardware uses a resistor divider, set the multiplier here.
// The demo used a factor of 3.0; adjust to match your hardware.
#define BATTERY_VOLTAGE_DIVIDER 3.0f

/**
 * @brief Convert ADC reading to battery voltage in mV
 */
static uint16_t adc_to_voltage_mv(uint32_t adc_raw) {
    int voltage_mv = 0;
    if (adc_cali_handle) {
        adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage_mv);
    } else {
        // Fallback: scale raw ADC reading (not calibrated)
        voltage_mv = (int)((adc_raw * 3300) / 4095);
    }
    return (uint16_t)voltage_mv;
}

/**
 * @brief Convert voltage to percentage (0-100)
 */
static uint8_t voltage_to_percentage(uint16_t voltage_mv) {
    if (voltage_mv <= BATTERY_MIN_MV) {
        return 0;
    }
    if (voltage_mv >= BATTERY_MAX_MV) {
        return 100;
    }

    // Linear interpolation
    uint16_t range = BATTERY_MAX_MV - BATTERY_MIN_MV;
    uint16_t current = voltage_mv - BATTERY_MIN_MV;
    return (uint8_t)((current * 100) / range);
}

/**
 * @brief Battery monitoring task (periodic reading)
 *
 * NOTE: Disabled for now due to ADC API complexity in ESP-IDF 6.1
 * Will be reimplemented with proper ADC continuous mode setup
 */
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated)
    {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated)
    {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "ADC calibration success");
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated)
    {
        ESP_LOGW(TAG, "eFuse not burnt or calibration not supported, skipping calibration");
    }
    else
    {
        ESP_LOGE(TAG, "ADC calibration failed: %s", esp_err_to_name(ret));
    }

    return calibrated;
}

static void battery_monitor_task(void* arg) {
    (void)arg;

    ESP_LOGI(TAG, "Battery monitoring task: using ADC oneshot");

    while (1) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(adc1_handle, BATTERY_ADC_CHANNEL, &raw);
        if (err == ESP_OK) {
            uint16_t voltage_mv = adc_to_voltage_mv((uint32_t)raw);
            float voltage_v = (voltage_mv / 1000.0f) * BATTERY_VOLTAGE_DIVIDER;
            uint16_t measured_mv = (uint16_t)(voltage_v * 1000.0f);

            battery_level = voltage_to_percentage(measured_mv);
            battery_charging = !gpio_get_level(BATTERY_CHARGE_GPIO);

            app_state_set_battery(battery_level, battery_charging);
            ESP_LOGI(TAG, "Battery: raw=%d mv=%d (after divider %.3fV) pct=%d%% charging=%s", raw, measured_mv, voltage_v, battery_level, battery_charging ? "yes" : "no");
        } else {
            ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

esp_err_t battery_monitor_init(void) {
    ESP_LOGI(TAG, "Initializing battery monitor");

    // Initialize ADC oneshot unit and channel (safe for simple periodic reads)
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(ret));
    } else {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .bitwidth = ADC_BITWIDTH_DEFAULT,
            .atten = BATTERY_ADC_ATTEN,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BATTERY_ADC_CHANNEL, &chan_cfg));

        do_calibration = example_adc_calibration_init(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, BATTERY_ADC_ATTEN, &adc_cali_handle);
    }

    // Initialize charging detection GPIO (optional)
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << BATTERY_CHARGE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gpio_cfg);

    // Create battery monitor task
    BaseType_t task_ret = xTaskCreate(
        battery_monitor_task,
        "battery_monitor",
        2048,
        NULL,
        5,
        NULL
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create battery monitor task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Battery monitor initialized (stub mode)");
    return ESP_OK;
}

uint8_t battery_monitor_get_level(void) {
    return battery_level;
}

bool battery_monitor_is_charging(void) {
    // Check if charging pin is low (active low logic)
    return !gpio_get_level(BATTERY_CHARGE_GPIO);
}
