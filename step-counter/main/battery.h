#ifndef BATTERY_H
#define BATTERY_H

/**
 * @brief Initialize battery monitoring
 */
void battery_init(void);

/**
 * @brief Read battery voltage and raw ADC value
 *
 * @param voltage_v Output: battery voltage in volts
 * @param adc_raw_avg Output: average raw ADC value
 */
void read_battery(float *voltage_v, int *adc_raw_avg);

/**
 * @brief Estimate battery percentage from voltage
 *
 * @param mv Voltage in millivolts
 * @return Battery percentage in thousandths (e.g., 952 = 95.2%)
 */
int estimate_percentage_milli(float mv);

#endif // BATTERY_H
