#include "avoid_control.hpp"
#include "car_config.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "smartcar_time.hpp"
#include "speed_drive.hpp"
#include "trace_control.hpp"
#include "web_handler.hpp"

namespace {
constexpr char TAG[] = "smartcar_app";

void blink_status_led()
{
    if (SMARTCAR_STATUS_LED_GPIO == GPIO_NUM_NC) {
        return;
    }

    const gpio_config_t led_config = {
        .pin_bit_mask = 1ULL << SMARTCAR_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
        .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
#endif
    };
    ESP_ERROR_CHECK(gpio_config(&led_config));
    for (int count = 0; count < 3; ++count) {
        gpio_set_level(SMARTCAR_STATUS_LED_GPIO, 1);
        smartcar_delay_ms(150);
        gpio_set_level(SMARTCAR_STATUS_LED_GPIO, 0);
        smartcar_delay_ms(150);
    }
}
}

extern "C" void app_main()
{
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);

    ESP_LOGI(TAG, "starting native ESP-IDF smart car port");
    CarDrive.begin();
    Tracer.begin();
    Avoider.begin();
    WebSys.begin(SMARTCAR_AP_SSID, SMARTCAR_AP_PASSWORD);
    CarDrive.stop();
    blink_status_led();

    uint32_t last_status_ms = 0;
    while (true) {
        CarDrive.loop();

        WebSys.run_mode_iteration();

        const uint32_t now = smartcar_millis();
        if (now - last_status_ms >= 2000) {
            ESP_LOGI(TAG, "mode=%d speed=%.2f cm/s free_heap=%lu",
                static_cast<int>(WebSys.mode()),
                CarDrive.smoothed_speed_cm_s(),
                static_cast<unsigned long>(esp_get_free_heap_size()));
            last_status_ms = now;
        }
        smartcar_delay_ms(1);
    }
}
