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

extern "C" {
void servo_init(void);
void vision_api_init(void);
void kin_init(void);
void robot_core_init(void);
void robot_core_start_task(void);
}

namespace {
constexpr char TAG[] = "qrs_integrated";

uint64_t gpio_pin_mask(gpio_num_t gpio)
{
    return gpio == GPIO_NUM_NC ? 0ULL : (1ULL << static_cast<uint32_t>(gpio));
}

void init_nvs()
{
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);
}

void init_optional_power_outputs()
{
    if (SMARTCAR_VLT_ENABLE_GPIO == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "VLT enable pin is not assigned; hardware must tie VLT to 3.3 V");
    } else {
        const gpio_config_t vlt_config = {
            .pin_bit_mask = gpio_pin_mask(SMARTCAR_VLT_ENABLE_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
            .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
#endif
        };
        ESP_ERROR_CHECK(gpio_config(&vlt_config));
        ESP_ERROR_CHECK(gpio_set_level(SMARTCAR_VLT_ENABLE_GPIO, 1));
    }

    if (SMARTCAR_SB_PLUS_GPIO != GPIO_NUM_NC) {
        const gpio_config_t sb_config = {
            .pin_bit_mask = gpio_pin_mask(SMARTCAR_SB_PLUS_GPIO),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
            .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
#endif
        };
        ESP_ERROR_CHECK(gpio_config(&sb_config));
        ESP_ERROR_CHECK(gpio_set_level(SMARTCAR_SB_PLUS_GPIO, 0));
    }
}

void start_smartcar()
{
    ESP_LOGI(TAG, "starting smart-car obstacle avoidance subsystem");
    init_optional_power_outputs();
    CarDrive.begin();
    Tracer.begin();
    Avoider.begin();
    WebSys.begin(SMARTCAR_AP_SSID, SMARTCAR_AP_PASSWORD);
    WebSys.set_mode(SystemMode::kAvoid);
    CarDrive.stop();
}

void start_robot_arm()
{
    ESP_LOGI(TAG, "starting AI vision robot-arm subsystem");
    servo_init();
    smartcar_delay_ms(500);
    vision_api_init();
    kin_init();
    robot_core_init();
    robot_core_start_task();
}
}

extern "C" void app_main()
{
    init_nvs();

    ESP_LOGI(TAG, "=== QianRuShi integrated system starting ===");
    start_smartcar();
    start_robot_arm();

    ESP_LOGI(TAG, "integrated system ready: obstacle avoidance + edge guard + AI vision robot arm");

    uint32_t last_status_ms = 0;
    while (true) {
        CarDrive.loop();
        if (!Tracer.guard()) {
            WebSys.run_mode_iteration();
        }

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
