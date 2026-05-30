#include "trace_control.hpp"

#include "car_config.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "smartcar_time.hpp"
#include "speed_drive.hpp"

namespace {
constexpr char TAG[] = "edge_guard";

bool read_active(gpio_num_t gpio)
{
    if (gpio == GPIO_NUM_NC) {
        return false;
    }

    const int level = gpio_get_level(gpio);
    return SMARTCAR_EDGE_ACTIVE_LOW ? (level == 0) : (level != 0);
}
}

TraceControl Tracer;

void TraceControl::begin()
{
    uint64_t pins = 0;
    if (SMARTCAR_EDGE_LEFT_GPIO != GPIO_NUM_NC) {
        pins |= 1ULL << SMARTCAR_EDGE_LEFT_GPIO;
    }
    if (SMARTCAR_EDGE_RIGHT_GPIO != GPIO_NUM_NC) {
        pins |= 1ULL << SMARTCAR_EDGE_RIGHT_GPIO;
    }

    if (pins != 0) {
        const gpio_config_t config = {
            .pin_bit_mask = pins,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = SMARTCAR_EDGE_ACTIVE_LOW ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = SMARTCAR_EDGE_ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
            .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
#endif
        };
        ESP_ERROR_CHECK(gpio_config(&config));
    }

    active_ = false;
    state_ = RecoveryState::kIdle;
    ESP_LOGI(TAG, "edge guard ready");
}

bool TraceControl::left_edge_triggered() const
{
    return read_active(SMARTCAR_EDGE_LEFT_GPIO);
}

bool TraceControl::right_edge_triggered() const
{
    return read_active(SMARTCAR_EDGE_RIGHT_GPIO);
}

void TraceControl::start_recovery(bool left_edge, bool right_edge)
{
    active_ = true;
    state_ = RecoveryState::kBacking;
    state_start_ms_ = smartcar_millis();
    turn_left_ = right_edge && !left_edge;
    CarDrive.run(-SMARTCAR_EDGE_SPEED_BACK, -SMARTCAR_EDGE_SPEED_BACK);
    ESP_LOGW(TAG, "edge detected: left=%d right=%d", left_edge, right_edge);
}

bool TraceControl::guard()
{
    const uint32_t now = smartcar_millis();

    if (state_ == RecoveryState::kIdle) {
        const bool left_edge = left_edge_triggered();
        const bool right_edge = right_edge_triggered();
        if (!left_edge && !right_edge) {
            return false;
        }
        start_recovery(left_edge, right_edge);
        return true;
    }

    if (state_ == RecoveryState::kBacking) {
        if (now - state_start_ms_ < SMARTCAR_EDGE_BACK_MS) {
            CarDrive.run(-SMARTCAR_EDGE_SPEED_BACK, -SMARTCAR_EDGE_SPEED_BACK);
            return true;
        }

        state_ = RecoveryState::kTurning;
        state_start_ms_ = now;
    }

    if (now - state_start_ms_ < SMARTCAR_EDGE_TURN_MS) {
        if (turn_left_) {
            CarDrive.run(-SMARTCAR_EDGE_SPEED_TURN, SMARTCAR_EDGE_SPEED_TURN);
        } else {
            CarDrive.run(SMARTCAR_EDGE_SPEED_TURN, -SMARTCAR_EDGE_SPEED_TURN);
        }
        return true;
    }

    CarDrive.stop();
    active_ = false;
    state_ = RecoveryState::kIdle;
    return false;
}

void TraceControl::stop()
{
    if (active_) {
        CarDrive.stop();
    }
    active_ = false;
    state_ = RecoveryState::kIdle;
}

bool TraceControl::active() const
{
    return active_;
}
