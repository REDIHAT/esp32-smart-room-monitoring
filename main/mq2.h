#ifndef MQ2_H
#define MQ2_H

#include "esp_err.h"
#include "driver/adc.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t raw_adc;            // Raw 12-bit ADC reading (0 - 4095)
    uint32_t pin_voltage_mv;     // Calibrated voltage measured at ESP32 D34 (mV)
    uint32_t sensor_ao_mv;       // Reconstructed MQ-2 AO voltage before divider (mV)
    bool is_gas_warning;         // True if reading exceeds configured alert threshold
} mq2_reading_t;

/**
 * @brief Initialize the ADC1 channel for MQ-2 reading on GPIO 34 (ADC1_CHANNEL_6).
 * @param channel The ADC1 channel (typically ADC1_CHANNEL_6 for D34).
 */
esp_err_t mq2_init(adc1_channel_t channel);

/**
 * @brief Sample the MQ-2 sensor with multisampling noise filtering and divider scaling.
 * @param reading Pointer to mq2_reading_t to receive calculated metrics.
 * @param raw_threshold Threshold for triggering a gas alert.
 */
esp_err_t mq2_read(mq2_reading_t *reading, uint32_t raw_threshold);

#ifdef __cplusplus
}
#endif

#endif // MQ2_H
