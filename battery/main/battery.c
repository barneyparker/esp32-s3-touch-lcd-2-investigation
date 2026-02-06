#include "battery.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BAT_ADC_CHANNEL ADC_CHANNEL_4
#define BAT_ADC_ATTEN ADC_ATTEN_DB_12
#define BAT_ADC_UNIT ADC_UNIT_1
#define BAT_ADC_SAMPLES 8
#define BAT_VOLTAGE_DIVIDER_FACTOR 3.0f
#define BAT_V_EMPTY_MV 3000
#define BAT_V_FULL_MV 4200

static const char *TAG = "battery";
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_chan_handle = NULL;
static bool do_calibration = false;

void battery_init(void)
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

void read_battery(float *voltage_v, int *adc_raw_avg)
{
  long sum_raw = 0;
  for (int i = 0; i < BAT_ADC_SAMPLES; ++i)
  {
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BAT_ADC_CHANNEL, &raw));
    sum_raw += raw;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  int avg_raw = (int)(sum_raw / BAT_ADC_SAMPLES);
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

int estimate_percentage_milli(float mv)
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
