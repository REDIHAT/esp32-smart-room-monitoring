#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_timer.h"

#include "dht11.h"
#include "mq2.h"
#include "dashboard_html.h"

static const char *TAG = "MAIN_APP";

/* ====================================================================
 * USER CONFIGURATION: Wi-Fi & Blynk Credentials (Placeholders)
 * ==================================================================== */
#define WIFI_SSID             "vivo Y400 Pro 5G"
#define WIFI_PASSWORD         "mouni@123"

#define BLYNK_TEMPLATE_ID     "TMPL38FOKpu8D"
#define BLYNK_TEMPLATE_NAME   "Smart Room Monitor"
#define BLYNK_AUTH_TOKEN      "OnBPNkwafu6QmLKD0-WH9kXWvZlh3R-i"
#define BLYNK_SERVER_HOST     "blr1.blynk.cloud"

/* Hardware Pin Assignments */
#define DHT11_GPIO_PIN        GPIO_NUM_4
#define MQ2_ADC_CHANNEL       ADC1_CHANNEL_6     // GPIO 34 (D34)

/* Sensor Operating Thresholds & Intervals */
#define MQ2_RAW_THRESHOLD     1200               // Configurable alert threshold on raw ADC (0-4095)
#define SENSOR_POLL_RATE_MS   2500               // Sensor sampling interval (ms)
#define BLYNK_UPDATE_RATE_MS  10000              // Blynk sync interval (ms) to avoid rate limits

/* FreeRTOS Event Group for Wi-Fi Sync */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1
#define MAXIMUM_RETRY         10

static int s_retry_num = 0;

/* Shared Sensor State Protected by Mutex */
static portMUX_TYPE s_sensor_spinlock = portMUX_INITIALIZER_UNLOCKED;
static dht11_reading_t s_current_dht = { .temperature = 0.0f, .humidity = 0.0f, .is_valid = false };
static mq2_reading_t   s_current_mq2 = { .raw_adc = 0, .pin_voltage_mv = 0, .sensor_ao_mv = 0, .is_gas_warning = false };

/* ====================================================================
 * Wi-Fi Event Handler & Reconnect Logic
 * ==================================================================== */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s...", WIFI_SSID);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting... (Attempt %d/%d)", s_retry_num, MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to Wi-Fi SSID: %s. Verify credentials and router.", WIFI_SSID);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, "Wi-Fi Connected! Local IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Web Dashboard available at: http://" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "==================================================");
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ====================================================================
 * Local HTTP Web Server Handlers (GET / and GET /api/data)
 * ==================================================================== */
static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_data_get_handler(httpd_req_t *req) {
    dht11_reading_t dht_copy;
    mq2_reading_t   mq2_copy;

    // Safely snapshot shared state
    portENTER_CRITICAL(&s_sensor_spinlock);
    dht_copy = s_current_dht;
    mq2_copy = s_current_mq2;
    portEXIT_CRITICAL(&s_sensor_spinlock);

    // Retrieve Wi-Fi RSSI signal strength
    wifi_ap_record_t ap_info;
    int8_t rssi = -100;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    char json_resp[300];
    snprintf(json_resp, sizeof(json_resp),
             "{"
             "\"dht_ok\":%s,"
             "\"temperature\":%.1f,"
             "\"humidity\":%.1f,"
             "\"gas_raw\":%u,"
             "\"gas_pin_mv\":%u,"
             "\"gas_ao_mv\":%u,"
             "\"gas_warning\":%s,"
             "\"wifi_rssi\":%d,"
             "\"uptime_sec\":%u"
             "}",
             dht_copy.is_valid ? "true" : "false",
             dht_copy.temperature,
             dht_copy.humidity,
             mq2_copy.raw_adc,
             mq2_copy.pin_voltage_mv,
             mq2_copy.sensor_ao_mv,
             mq2_copy.is_gas_warning ? "true" : "false",
             rssi,
             uptime_sec);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_resp, HTTPD_RESP_USE_STRLEN);
}

static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP server on port %d...", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_index = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_index);

        httpd_uri_t uri_api = {
            .uri       = "/api/data",
            .method    = HTTP_GET,
            .handler   = api_data_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_api);
        return server;
    }

    ESP_LOGE(TAG, "Error starting HTTP server!");
    return NULL;
}

/* ====================================================================
 * FreeRTOS Background Task: Sensor Sampling
 * ==================================================================== */
static void sensor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Initializing hardware sensors...");
    ESP_ERROR_CHECK(dht11_init(DHT11_GPIO_PIN));
    ESP_ERROR_CHECK(mq2_init(MQ2_ADC_CHANNEL));
    ESP_LOGI(TAG, "Sensors initialized successfully.");

    while (1) {
        dht11_reading_t local_dht;
        mq2_reading_t   local_mq2;

        // 1. Read DHT11
        esp_err_t dht_err = dht11_read(&local_dht);
        if (dht_err != ESP_OK) {
            ESP_LOGW(TAG, "DHT11 read warning: %s", esp_err_to_name(dht_err));
        }

        // 2. Read MQ-2 via ADC
        esp_err_t mq2_err = mq2_read(&local_mq2, MQ2_RAW_THRESHOLD);
        if (mq2_err != ESP_OK) {
            ESP_LOGW(TAG, "MQ2 read warning: %s", esp_err_to_name(mq2_err));
        }

        // 3. Atomically update shared application state
        portENTER_CRITICAL(&s_sensor_spinlock);
        if (dht_err == ESP_OK) {
            s_current_dht = local_dht;
        } else {
            s_current_dht.is_valid = false;
        }
        if (mq2_err == ESP_OK) {
            s_current_mq2 = local_mq2;
        }
        portEXIT_CRITICAL(&s_sensor_spinlock);

        ESP_LOGI(TAG, "[SENSOR LOG] Temp: %.1f C | Hum: %.1f %% | Gas Raw: %u | Pin: %u mV | Sensor AO: %u mV | Status: %s",
                 s_current_dht.temperature,
                 s_current_dht.humidity,
                 s_current_mq2.raw_adc,
                 s_current_mq2.pin_voltage_mv,
                 s_current_mq2.sensor_ao_mv,
                 s_current_mq2.is_gas_warning ? "WARNING" : "NORMAL");

        vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_RATE_MS));
    }
}

/* ====================================================================
 * FreeRTOS Background Task: Blynk IoT Cloud Synchronization
 * ==================================================================== */
static void blynk_task(void *pvParameters) {
    char url_buffer[384];

    while (1) {
        // Wait until Wi-Fi is connected
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                              WIFI_CONNECTED_BIT,
                                              pdFALSE,
                                              pdFALSE,
                                              portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT) {
            dht11_reading_t dht;
            mq2_reading_t   mq2;

            portENTER_CRITICAL(&s_sensor_spinlock);
            dht = s_current_dht;
            mq2 = s_current_mq2;
            portEXIT_CRITICAL(&s_sensor_spinlock);

            // Virtual pin assignment:
            // V0: Temperature (deg C)
            // V1: Humidity (%)
            // V2: MQ-2 Raw ADC (0 - 4095)
            // V3: Gas Alert Status (0 = Normal, 1 = Warning)
            // V4: Sensor AO Voltage (mV)
            snprintf(url_buffer, sizeof(url_buffer),
                     "http://%s/external/api/batch/update?token=%s&v0=%.1f&v1=%.1f&v2=%u&v3=%d&v4=%u",
                     BLYNK_SERVER_HOST,
                     BLYNK_AUTH_TOKEN,
                     dht.is_valid ? dht.temperature : 0.0f,
                     dht.is_valid ? dht.humidity : 0.0f,
                     mq2.raw_adc,
                     mq2.is_gas_warning ? 1 : 0,
                     mq2.sensor_ao_mv);

            esp_http_client_config_t config = {
                .url = url_buffer,
                .method = HTTP_METHOD_GET,
                .timeout_ms = 5000,
            };

            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (client != NULL) {
                esp_err_t err = esp_http_client_perform(client);
                if (err == ESP_OK) {
                    int status_code = esp_http_client_get_status_code(client);
                    if (status_code == 200) {
                        ESP_LOGD(TAG, "Blynk sync successful (HTTP 200)");
                    } else {
                        ESP_LOGW(TAG, "Blynk server responded with status: %d", status_code);
                    }
                } else {
                    ESP_LOGW(TAG, "Blynk HTTP request failed: %s", esp_err_to_name(err));
                }
                esp_http_client_cleanup(client);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BLYNK_UPDATE_RATE_MS));
    }
}

/* ====================================================================
 * Main Application Entry Point
 * ==================================================================== */
void app_main(void) {
    ESP_LOGI(TAG, "Starting ESP32 Smart Room & Gas Monitoring System...");

    // 1. Initialize Non-Volatile Storage (NVS) for Wi-Fi stack
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Start Wi-Fi Station
    wifi_init_sta();

    // 3. Start Local HTTP Web Server
    start_webserver();

    // 4. Launch FreeRTOS Sensor Reading Task (Pinned to Core 1)
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, NULL, 5, NULL, 1);

    // 5. Launch FreeRTOS Blynk Sync Task (Pinned to Core 0)
    xTaskCreatePinnedToCore(blynk_task, "blynk_task", 4096, NULL, 4, NULL, 0);

    ESP_LOGI(TAG, "System operational. All background tasks active.");
}
