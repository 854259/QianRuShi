#include "speed_drive.hpp"

#include <algorithm>
#include <cmath>

#include "car_config.hpp"
#include "esp_check.h"
#include "esp_log.h"
#include "smartcar_time.hpp"

namespace {
constexpr char TAG[] = "smartcar_drive";
constexpr ledc_mode_t MOTOR_SPEED_MODE = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t MOTOR_TIMER = LEDC_TIMER_0;

uint64_t gpio_pin_mask(gpio_num_t gpio)
{
    return gpio == GPIO_NUM_NC ? 0ULL : (1ULL << static_cast<uint32_t>(gpio));
}
}

SpeedDrive CarDrive;

void IRAM_ATTR SpeedDrive::speed_sensor_isr(void *arg)
{
    auto *drive = static_cast<SpeedDrive *>(arg);
    const uint64_t now = smartcar_micros();

    portENTER_CRITICAL_ISR(&drive->pulse_lock_);
    if (now - drive->last_isr_us_ > 1000) {
        drive->pulse_count_ = drive->pulse_count_ + 1;
        drive->last_isr_us_ = now;
    }
    portEXIT_CRITICAL_ISR(&drive->pulse_lock_);
}

void SpeedDrive::begin()
{
    motor_outputs_ = {{
        {SMARTCAR_MOTO_LF_A_GPIO, LEDC_CHANNEL_0},
        {SMARTCAR_MOTO_LF_B_GPIO, LEDC_CHANNEL_1},
        {SMARTCAR_MOTO_RF_A_GPIO, LEDC_CHANNEL_2},
        {SMARTCAR_MOTO_RF_B_GPIO, LEDC_CHANNEL_3},
        {SMARTCAR_MOTO_LR_A_GPIO, LEDC_CHANNEL_4},
        {SMARTCAR_MOTO_LR_B_GPIO, LEDC_CHANNEL_5},
        {SMARTCAR_MOTO_RR_A_GPIO, LEDC_CHANNEL_6},
        {SMARTCAR_MOTO_RR_B_GPIO, LEDC_CHANNEL_7},
    }};

    const ledc_timer_config_t timer_config = {
        .speed_mode = MOTOR_SPEED_MODE,
        .duty_resolution = SMARTCAR_MOTOR_PWM_RESOLUTION,
        .timer_num = MOTOR_TIMER,
        .freq_hz = SMARTCAR_MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    for (const auto &output : motor_outputs_) {
        const ledc_channel_config_t channel_config = {
            .gpio_num = output.gpio,
            .speed_mode = MOTOR_SPEED_MODE,
            .channel = output.channel,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = MOTOR_TIMER,
            .duty = 0,
            .hpoint = 0,
            .flags = {},
        };
        ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
    }

    if (SMARTCAR_SPEED_SENSOR_GPIO != GPIO_NUM_NC) {
        const gpio_config_t speed_sensor_config = {
            .pin_bit_mask = gpio_pin_mask(SMARTCAR_SPEED_SENSOR_GPIO),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_POSEDGE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
            .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
#endif
        };
        ESP_ERROR_CHECK(gpio_config(&speed_sensor_config));

        esp_err_t isr_result = gpio_install_isr_service(0);
        if (isr_result != ESP_OK && isr_result != ESP_ERR_INVALID_STATE) {
            ESP_ERROR_CHECK(isr_result);
        }
        ESP_ERROR_CHECK(gpio_isr_handler_add(SMARTCAR_SPEED_SENSOR_GPIO, speed_sensor_isr, this));
        ESP_LOGI(TAG, "motor PWM and speed sensor ready");
    } else {
        ESP_LOGI(TAG, "motor PWM ready; speed sensor disabled");
    }

    pulse_count_ = 0;
    last_isr_us_ = 0;
    last_calc_ms_ = smartcar_millis();
}

void SpeedDrive::set_motor_output(const MotorOutput &output, uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(MOTOR_SPEED_MODE, output.channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(MOTOR_SPEED_MODE, output.channel));
}

void SpeedDrive::set_motor_group(size_t first_index, size_t second_index, int speed)
{
    const uint32_t duty = static_cast<uint32_t>(std::clamp(std::abs(speed), 0, SMARTCAR_MAX_PWM));
    const bool forward = speed > 0;
    const bool reverse = speed < 0;

    set_motor_output(motor_outputs_[first_index], forward ? duty : 0);
    set_motor_output(motor_outputs_[first_index + 1], reverse ? duty : 0);
    set_motor_output(motor_outputs_[second_index], forward ? duty : 0);
    set_motor_output(motor_outputs_[second_index + 1], reverse ? duty : 0);
}

void SpeedDrive::run(int left_speed, int right_speed)
{
    left_speed = std::clamp(left_speed, -SMARTCAR_MAX_PWM, SMARTCAR_MAX_PWM);
    right_speed = std::clamp(right_speed, -SMARTCAR_MAX_PWM, SMARTCAR_MAX_PWM);
    set_motor_group(0, 4, left_speed);
    set_motor_group(2, 6, right_speed);
}

void SpeedDrive::stop()
{
    for (const auto &output : motor_outputs_) {
        set_motor_output(output, 0);
    }
    current_speed_ = 0.0F;
    smoothed_speed_ = 0.0F;
}

void SpeedDrive::loop()
{
    if (SMARTCAR_SPEED_SENSOR_GPIO == GPIO_NUM_NC) {
        current_speed_ = 0.0F;
        smoothed_speed_ = 0.0F;
        return;
    }

    const uint32_t now = smartcar_millis();
    if (now - last_calc_ms_ < SMARTCAR_SPEED_CALC_INTERVAL_MS) {
        return;
    }

    uint32_t raw_pulses = 0;
    portENTER_CRITICAL(&pulse_lock_);
    raw_pulses = pulse_count_;
    pulse_count_ = 0;
    portEXIT_CRITICAL(&pulse_lock_);

    if (raw_pulses >= SMARTCAR_MIN_PULSE_FOR_SPEED) {
        filtered_pulse_ = (SMARTCAR_EMA_ALPHA * raw_pulses)
            + ((1.0F - SMARTCAR_EMA_ALPHA) * filtered_pulse_);
        const float speed = filtered_pulse_
            * (1000.0F / SMARTCAR_SPEED_CALC_INTERVAL_MS)
            * SMARTCAR_CM_PER_PULSE;
        smoothed_speed_ = (0.7F * speed) + (0.3F * smoothed_speed_);
        current_speed_ = speed;
    } else {
        filtered_pulse_ *= 0.5F;
        current_speed_ = filtered_pulse_
            * (1000.0F / SMARTCAR_SPEED_CALC_INTERVAL_MS)
            * SMARTCAR_CM_PER_PULSE;
        smoothed_speed_ *= 0.7F;
    }

    last_calc_ms_ = now;
}

float SpeedDrive::speed_cm_s() const
{
    return current_speed_;
}

float SpeedDrive::smoothed_speed_cm_s() const
{
    return smoothed_speed_;
}

bool SpeedDrive::speed_stable()
{
    const float diff = std::fabs(current_speed_ - last_speed_);
    last_speed_ = current_speed_;
    return diff < SMARTCAR_SPEED_THRESHOLD_CM_S;
}
