#include "dht11.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

static const char *TAG = "DHT11";
static gpio_num_t s_dht_gpio = GPIO_NUM_4;

esp_err_t dht11_init(gpio_num_t gpio_pin) {
    s_dht_gpio = gpio_pin;
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_dht_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    return gpio_config(&io_conf);
}

static int wait_for_level(int expected_level, int timeout_us) {
    int elapsed = 0;
    while (gpio_get_level(s_dht_gpio) != expected_level) {
        if (elapsed >= timeout_us) {
            return -1;
        }
        ets_delay_us(2);
        elapsed += 2;
    }
    return elapsed;
}

esp_err_t dht11_read(dht11_reading_t *reading) {
    if (reading == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[5] = {0};
    reading->is_valid = false;

    // 1. Host pulls line LOW for at least 18 ms to send start signal
    gpio_set_direction(s_dht_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(s_dht_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    // 2. Host pulls HIGH for 20-40 us, then switches to INPUT
    gpio_set_level(s_dht_gpio, 1);
    ets_delay_us(30);
    gpio_set_direction(s_dht_gpio, GPIO_MODE_INPUT);

    // 3. DHT11 pulls LOW for 80 us, then HIGH for 80 us
    if (wait_for_level(0, 100) < 0) {
        ESP_LOGD(TAG, "Timeout waiting for DHT11 initial response low");
        return ESP_ERR_TIMEOUT;
    }
    if (wait_for_level(1, 100) < 0) {
        ESP_LOGD(TAG, "Timeout waiting for DHT11 initial response high");
        return ESP_ERR_TIMEOUT;
    }
    if (wait_for_level(0, 100) < 0) {
        ESP_LOGD(TAG, "Timeout waiting for DHT11 transmission start");
        return ESP_ERR_TIMEOUT;
    }

    // 4. Read 40 data bits (5 bytes)
    for (int bit_idx = 0; bit_idx < 40; bit_idx++) {
        // Each bit starts with a 50 us low pulse
        if (wait_for_level(1, 70) < 0) {
            ESP_LOGD(TAG, "Timeout waiting for bit %d high pulse", bit_idx);
            return ESP_ERR_TIMEOUT;
        }

        // Measure length of high pulse
        int64_t start_time = esp_timer_get_time();
        if (wait_for_level(0, 100) < 0) {
            ESP_LOGD(TAG, "Timeout waiting for bit %d low transition", bit_idx);
            return ESP_ERR_TIMEOUT;
        }
        int64_t duration = esp_timer_get_time() - start_time;

        // ~26-28 us pulse = 0; ~70 us pulse = 1
        int byte_idx = bit_idx / 8;
        data[byte_idx] <<= 1;
        if (duration > 45) {
            data[byte_idx] |= 1;
        }
    }

    // 5. Verify Checksum: Byte 4 == (Byte 0 + Byte 1 + Byte 2 + Byte 3) & 0xFF
    uint8_t checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    if (data[4] != checksum) {
        ESP_LOGW(TAG, "CRC Error: calculated 0x%02X, received 0x%02X", checksum, data[4]);
        return ESP_ERR_INVALID_CRC;
    }

    reading->humidity = (float)data[0] + ((float)data[1] * 0.1f);
    reading->temperature = (float)data[2] + ((float)data[3] * 0.1f);
    reading->is_valid = true;

    return ESP_OK;
}
