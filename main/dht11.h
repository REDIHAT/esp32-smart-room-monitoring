#ifndef DHT11_H
#define DHT11_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature;  // Temperature in degrees Celsius
    float humidity;     // Relative humidity in %
    bool is_valid;      // True if the last checksum and read succeeded
} dht11_reading_t;

/**
 * @brief Initialize the GPIO pin for DHT11 communication.
 * @param gpio_pin The GPIO pin connected to the DHT11 DATA line (e.g., GPIO_NUM_4).
 */
esp_err_t dht11_init(gpio_num_t gpio_pin);

/**
 * @brief Read temperature and humidity from the DHT11 sensor.
 * @param reading Pointer to dht11_reading_t where results will be stored.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on communication failure, ESP_ERR_INVALID_CRC on checksum mismatch.
 */
esp_err_t dht11_read(dht11_reading_t *reading);

#ifdef __cplusplus
}
#endif

#endif // DHT11_H
