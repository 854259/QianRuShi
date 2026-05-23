#include "avoid_control.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

#include "car_config.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "smartcar_time.hpp"
#include "speed_drive.hpp"

namespace {
constexpr char TAG[] = "smartcar_avoid";
constexpr uint32_t SERVO_RESOLUTION_HZ = 1000000;
constexpr uint32_t SERVO_PERIOD_US = 20000;
constexpr std::array<int, 4> SWEEP_ANGLES = {30, 90, 150, 90};

bool wait_for_gpio_level(gpio_num_t gpio, int level, uint32_t timeout_us)
{
    const uint64_t start = smartcar_micros();
    while (gpio_get_level(gpio) != level) {
        if (smartcar_micros() - start >= timeout_us) {
            return false;
        }
    }
    return true;
}
}

AvoidControl Avoider;

void AvoidControl::begin()
{
    const gpio_config_t ultrasonic_config = {
        .pin_bit_mask = (1ULL << SMARTCAR_SR04_TRIG_GPIO) | (1ULL << SMARTCAR_SR04_ECHO_GPIO),
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
        .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
#endif
    };
    ESP_ERROR_CHECK(gpio_config(&ultrasonic_config));
    ESP_ERROR_CHECK(gpio_set_direction(SMARTCAR_SR04_TRIG_GPIO, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_direction(SMARTCAR_SR04_ECHO_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_level(SMARTCAR_SR04_TRIG_GPIO, 0));

    const mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = SERVO_RESOLUTION_HZ,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
        .period_ticks = SERVO_PERIOD_US,
        .intr_priority = 0,
        .flags = {},
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &servo_timer_));

    const mcpwm_operator_config_t operator_config = {
        .group_id = 0,
        .intr_priority = 0,
        .flags = {},
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &servo_operator_));
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(servo_operator_, servo_timer_));

    const mcpwm_comparator_config_t comparator_config = {
        .intr_priority = 0,
        .flags = {
            .update_cmp_on_tez = true,
            .update_cmp_on_tep = false,
            .update_cmp_on_sync = false,
        },
    };
    ESP_ERROR_CHECK(mcpwm_new_comparator(servo_operator_, &comparator_config, &servo_comparator_));

    const mcpwm_generator_config_t generator_config = {
        .gen_gpio_num = SMARTCAR_SERVO_GPIO,
        .flags = {},
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(servo_operator_, &generator_config, &servo_generator_));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(
        servo_generator_,
        MCPWM_GEN_TIMER_EVENT_ACTION(
            MCPWM_TIMER_DIRECTION_UP,
            MCPWM_TIMER_EVENT_EMPTY,
            MCPWM_GEN_ACTION_HIGH)));
    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(
        servo_generator_,
        MCPWM_GEN_COMPARE_EVENT_ACTION(
            MCPWM_TIMER_DIRECTION_UP,
            servo_comparator_,
            MCPWM_GEN_ACTION_LOW)));
    ESP_ERROR_CHECK(mcpwm_timer_enable(servo_timer_));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(servo_timer_, MCPWM_TIMER_START_NO_STOP));

    current_servo_angle_ = 90;
    servo_write(90);
    active_ = false;
    last_turn_left_ = false;
    last_turn_direction_ = 0;
    sweep_step_ = 0;
    last_sweep_ms_ = 0;
    distance_left_cm_ = 200;
    distance_center_cm_ = 200;
    distance_right_cm_ = 200;
    state_ = AvoidState::kForward;
    state_start_ms_ = 0;
    ESP_LOGI(TAG, "ultrasonic and servo ready");
}

void AvoidControl::servo_write(int angle)
{
    angle = std::clamp(angle, 0, 180);
    const int delta = std::abs(angle - current_servo_angle_);
    current_servo_angle_ = angle;
    const uint32_t pulse_width_us = 500 + ((2000 * angle) / 180);
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(servo_comparator_, pulse_width_us));
    servo_ready_ms_ = smartcar_millis() + std::clamp(delta * 3, 80, 500);
}

int AvoidControl::distance_cm()
{
    gpio_set_level(SMARTCAR_SR04_TRIG_GPIO, 0);
    smartcar_delay_us(2);
    gpio_set_level(SMARTCAR_SR04_TRIG_GPIO, 1);
    smartcar_delay_us(10);
    gpio_set_level(SMARTCAR_SR04_TRIG_GPIO, 0);

    if (!wait_for_gpio_level(SMARTCAR_SR04_ECHO_GPIO, 1, 25000)) {
        return 200;
    }

    const uint64_t pulse_start = smartcar_micros();
    if (!wait_for_gpio_level(SMARTCAR_SR04_ECHO_GPIO, 0, 25000)) {
        return 200;
    }

    const uint64_t duration_us = smartcar_micros() - pulse_start;
    return std::clamp(static_cast<int>((duration_us * 34ULL) / 2000ULL), 0, 200);
}

void AvoidControl::update_sweep()
{
    const uint32_t now = smartcar_millis();
    if (sweep_measure_pending_) {
        if (static_cast<int32_t>(now - servo_ready_ms_) < 0) {
            return;
        }

        const int angle = SWEEP_ANGLES[sweep_step_];
        const int measured_distance = distance_cm();

        if (angle <= 45) {
            distance_right_cm_ = measured_distance;
        } else if (angle >= 135) {
            distance_left_cm_ = measured_distance;
        } else {
            distance_center_cm_ = measured_distance;
        }

        ESP_LOGI(TAG, "scan angle=%d left=%d center=%d right=%d",
            angle, distance_left_cm_, distance_center_cm_, distance_right_cm_);
        sweep_step_ = (sweep_step_ + 1) % SWEEP_ANGLES.size();
        last_sweep_ms_ = now;
        sweep_measure_pending_ = false;
        return;
    }

    if (now - last_sweep_ms_ < SMARTCAR_AVOID_SWEEP_STEP_MS) {
        return;
    }

    servo_write(SWEEP_ANGLES[sweep_step_]);
    sweep_measure_pending_ = true;
}

AvoidControl::AvoidState AvoidControl::decide_turn_state()
{
    if (distance_left_cm_ < SMARTCAR_AVOID_DISTANCE_SLOW_CM
        && distance_center_cm_ < SMARTCAR_AVOID_DISTANCE_EMERGENCY_CM
        && distance_right_cm_ < SMARTCAR_AVOID_DISTANCE_SLOW_CM) {
        last_turn_direction_ = 0;
        return AvoidState::kBacking;
    }

    const int left_score = distance_left_cm_ + (last_turn_direction_ == -1 ? 10 : 0);
    const int right_score = distance_right_cm_ + (last_turn_direction_ == 1 ? 10 : 0);
    last_turn_left_ = left_score >= right_score;
    last_turn_direction_ = last_turn_left_ ? -1 : 1;
    return AvoidState::kTurning;
}

void AvoidControl::handle_forward()
{
    const int distance = distance_center_cm_;
    if (distance < SMARTCAR_AVOID_DISTANCE_EMERGENCY_CM) {
        CarDrive.stop();
        state_ = decide_turn_state();
        state_start_ms_ = smartcar_millis();
    } else if (distance < SMARTCAR_AVOID_DISTANCE_SLOW_CM) {
        CarDrive.run(SMARTCAR_AVOID_SPEED_SLOW - 20, SMARTCAR_AVOID_SPEED_SLOW - 20);
    } else if (distance < SMARTCAR_AVOID_DISTANCE_SAFE_CM) {
        if (distance_left_cm_ > distance_right_cm_ + 20) {
            CarDrive.run(SMARTCAR_AVOID_SPEED_SLOW - 20, SMARTCAR_AVOID_SPEED_SLOW);
        } else if (distance_right_cm_ > distance_left_cm_ + 20) {
            CarDrive.run(SMARTCAR_AVOID_SPEED_SLOW, SMARTCAR_AVOID_SPEED_SLOW - 20);
        } else {
            CarDrive.run(SMARTCAR_AVOID_SPEED_SLOW, SMARTCAR_AVOID_SPEED_SLOW);
        }
    } else {
        CarDrive.run(SMARTCAR_AVOID_SPEED_NORMAL, SMARTCAR_AVOID_SPEED_NORMAL);
    }
}

void AvoidControl::handle_turning()
{
    const uint32_t elapsed = smartcar_millis() - state_start_ms_;
    if (last_turn_left_) {
        CarDrive.run(-SMARTCAR_AVOID_SPEED_TURN, SMARTCAR_AVOID_SPEED_TURN);
    } else {
        CarDrive.run(SMARTCAR_AVOID_SPEED_TURN, -SMARTCAR_AVOID_SPEED_TURN);
    }

    if (elapsed < 400) {
        return;
    }

    if (distance_center_cm_ > SMARTCAR_AVOID_DISTANCE_SAFE_CM) {
        CarDrive.stop();
        smartcar_delay_ms(80);
        state_ = AvoidState::kForward;
        state_start_ms_ = smartcar_millis();
    } else if (elapsed > 1800) {
        CarDrive.stop();
        smartcar_delay_ms(80);
        state_ = decide_turn_state();
        state_start_ms_ = smartcar_millis();
    }
}

void AvoidControl::handle_backing()
{
    if (smartcar_millis() - state_start_ms_ < SMARTCAR_AVOID_BACK_MS) {
        CarDrive.run(-SMARTCAR_AVOID_SPEED_SLOW, -SMARTCAR_AVOID_SPEED_SLOW);
        return;
    }

    CarDrive.stop();
    smartcar_delay_ms(80);
    last_turn_direction_ = distance_left_cm_ >= distance_right_cm_ ? -1 : 1;
    last_turn_left_ = last_turn_direction_ == -1;
    state_ = AvoidState::kUTurn;
    state_start_ms_ = smartcar_millis();
}

void AvoidControl::handle_u_turn()
{
    if (last_turn_direction_ == -1) {
        CarDrive.run(-SMARTCAR_AVOID_SPEED_TURN, SMARTCAR_AVOID_SPEED_TURN);
    } else {
        CarDrive.run(SMARTCAR_AVOID_SPEED_TURN, -SMARTCAR_AVOID_SPEED_TURN);
    }

    const uint32_t elapsed = smartcar_millis() - state_start_ms_;
    if (elapsed > 600 && distance_center_cm_ > SMARTCAR_AVOID_DISTANCE_SAFE_CM) {
        CarDrive.stop();
        smartcar_delay_ms(80);
        state_ = AvoidState::kForward;
        state_start_ms_ = smartcar_millis();
    } else if (elapsed > 3000) {
        CarDrive.stop();
        state_ = AvoidState::kBacking;
        state_start_ms_ = smartcar_millis();
    }
}

void AvoidControl::run()
{
    if (!active_) {
        active_ = true;
        state_ = AvoidState::kForward;
        sweep_step_ = 0;
        last_sweep_ms_ = 0;
        sweep_measure_pending_ = false;
        distance_left_cm_ = 200;
        distance_center_cm_ = 200;
        distance_right_cm_ = 200;
        last_turn_direction_ = 0;
        last_turn_left_ = false;
        current_servo_angle_ = 90;
        servo_write(90);
        ESP_LOGI(TAG, "avoid mode active");
    }

    update_sweep();
    switch (state_) {
    case AvoidState::kForward:
        handle_forward();
        break;
    case AvoidState::kTurning:
        handle_turning();
        break;
    case AvoidState::kBacking:
        handle_backing();
        break;
    case AvoidState::kUTurn:
        handle_u_turn();
        break;
    }
}

void AvoidControl::stop()
{
    if (active_) {
        CarDrive.stop();
        current_servo_angle_ = 90;
        servo_write(90);
        active_ = false;
        sweep_measure_pending_ = false;
        state_ = AvoidState::kForward;
        last_turn_direction_ = 0;
        ESP_LOGI(TAG, "avoid mode stopped");
    }
}

bool AvoidControl::active() const
{
    return active_;
}
