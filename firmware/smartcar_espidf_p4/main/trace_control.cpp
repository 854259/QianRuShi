#include "trace_control.hpp"

#include <numeric>

#include "car_config.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "speed_drive.hpp"

namespace {
constexpr char TAG[] = "smartcar_trace";
}

TraceControl Tracer;

void TraceControl::begin()
{
    uint64_t pins = 0;
    for (gpio_num_t gpio : SMARTCAR_TRACE_GPIO) {
        pins |= 1ULL << gpio;
    }

    const gpio_config_t config = {
        .pin_bit_mask = pins,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
        .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
#endif
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    active_ = false;
}

TraceControl::SensorData TraceControl::read_sensors() const
{
    SensorData sensors{};
    for (size_t index = 0; index < sensors.size(); ++index) {
        // Preserve the original active-low trace sensor convention.
        sensors[index] = gpio_get_level(SMARTCAR_TRACE_GPIO[index]) == 0;
    }
    return sensors;
}

void TraceControl::execute_action(const SensorData &sensors)
{
    const int active_count = std::accumulate(sensors.begin(), sensors.end(), 0);

    if (active_count >= 4) {
        CarDrive.run(SMARTCAR_TRACE_SPEED_SLOW, SMARTCAR_TRACE_SPEED_SLOW);
        ESP_LOGI(TAG, "wide line or crossing");
        return;
    }

    if (active_count == 0) {
        CarDrive.stop();
        ESP_LOGI(TAG, "line lost");
        return;
    }

    const bool s1 = sensors[0];
    const bool s2 = sensors[1];
    const bool s3 = sensors[2];
    const bool s4 = sensors[3];
    const bool s5 = sensors[4];

    if (!s1 && !s2 && s3 && !s4 && !s5) {
        CarDrive.run(SMARTCAR_TRACE_SPEED_FAST, SMARTCAR_TRACE_SPEED_FAST);
    } else if (!s1 && s2 && s3 && !s4 && !s5) {
        CarDrive.run(SMARTCAR_TRACE_SPEED_MEDIUM, SMARTCAR_TRACE_SPEED_FAST);
    } else if (!s1 && !s2 && s3 && s4 && !s5) {
        CarDrive.run(SMARTCAR_TRACE_SPEED_FAST, SMARTCAR_TRACE_SPEED_MEDIUM);
    } else if (!s1 && s2 && !s3 && !s4 && !s5) {
        CarDrive.run(SMARTCAR_TRACE_SPEED_SLOW, SMARTCAR_TRACE_SPEED_FAST);
    } else if (!s1 && !s2 && !s3 && s4 && !s5) {
        CarDrive.run(SMARTCAR_TRACE_SPEED_FAST, SMARTCAR_TRACE_SPEED_SLOW);
    } else if (s1) {
        CarDrive.run(-SMARTCAR_TRACE_SPEED_REVERSE, SMARTCAR_TRACE_SPEED_TURN);
    } else if (s5) {
        CarDrive.run(SMARTCAR_TRACE_SPEED_TURN, -SMARTCAR_TRACE_SPEED_REVERSE);
    } else if (s2 || s3) {
        CarDrive.run(SMARTCAR_TRACE_SPEED_SLOW, SMARTCAR_TRACE_SPEED_MEDIUM);
    }
}

void TraceControl::run()
{
    if (!active_) {
        active_ = true;
        ESP_LOGI(TAG, "trace mode active");
    }
    execute_action(read_sensors());
}

void TraceControl::stop()
{
    if (active_) {
        CarDrive.stop();
        active_ = false;
        ESP_LOGI(TAG, "trace mode stopped");
    }
}

bool TraceControl::active() const
{
    return active_;
}
