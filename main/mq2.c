#include "mq2.h"
#include "esp_adc_cal.h"
#include "esp_log.h"

static const char *TAG = "MQ2";

#define NO_OF_SAMPLES       32     // Multisampling count for noise suppression
#define DEFAULT_VREF        1100   // eFuse Vref default in mV

// Divider resistor values: R1 = 22k, R2 = 10k
// Multiplier to restore original AO voltage: (22 + 10) / 10 = 3.2
#define DIVIDER_MULTIPLIER_NUM   32
#define DIVIDER_MULTIPLIER_DEN   10

static adc1_channel_t s_mq2_channel = ADC1_CHANNEL_6;
static esp_adc_cal_characteristics_t s_adc_chars;

esp_err_t mq2_init(adc1_channel_t channel) {
    s_mq2_channel = channel;

    // Configure 12-bit ADC width (0 - 4095)
    esp_err_t err = adc1_config_width(ADC_WIDTH_BIT_12);
    if (err != ESP_OK) return err;

    // Configure 11 dB attenuation (full scale range up to ~2.6V - 3.1V)
    err = adc1_config_channel_atten(s_mq2_channel, ADC_ATTEN_DB_11);
    if (err != ESP_OK) return err;

    // Characterize ADC using eFuse Vref if available
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_11,
        ADC_WIDTH_BIT_12,
        DEFAULT_VREF,
        &s_adc_chars
    );

    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "ADC characterized using eFuse Vref");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "ADC characterized using Two Point eFuse");
    } else {
        ESP_LOGI(TAG, "ADC characterized using Default Vref (%d mV)", DEFAULT_VREF);
    }

    return ESP_OK;
}

esp_err_t mq2_read(mq2_reading_t *reading, uint32_t raw_threshold) {
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t adc_accum = 0;
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        adc_accum += adc1_get_raw(s_mq2_channel);
    }
    reading->raw_adc = adc_accum / NO_OF_SAMPLES;

    // Convert raw ADC reading to calibrated millivolts at pin D34
    reading->pin_voltage_mv = esp_adc_cal_raw_to_voltage(reading->raw_adc, &s_adc_chars);

    // Compute original MQ-2 AO voltage by reversing the voltage divider: V_ao = V_pin * (32 / 10)
    reading->sensor_ao_mv = (reading->pin_voltage_mv * DIVIDER_MULTIPLIER_NUM) / DIVIDER_MULTIPLIER_DEN;

    // Evaluate gas warning status
    reading->is_gas_warning = (reading->raw_adc >= raw_threshold);

    return ESP_OK;
}
