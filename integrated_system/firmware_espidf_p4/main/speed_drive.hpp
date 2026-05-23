#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

class SpeedDrive {
public:
    void begin();
    void loop();

    void stop();
    void run(int left_speed, int right_speed);

    float speed_cm_s() const;
    float smoothed_speed_cm_s() const;
    bool speed_stable();

private:
    struct MotorOutput {
        gpio_num_t gpio;
        ledc_channel_t channel;
    };

    static void IRAM_ATTR speed_sensor_isr(void *arg);

    void set_motor_output(const MotorOutput &output, uint32_t duty);
    void set_motor_group(size_t first_index, size_t second_index, int speed);

    std::array<MotorOutput, 8> motor_outputs_{};
    portMUX_TYPE pulse_lock_ = portMUX_INITIALIZER_UNLOCKED;
    volatile uint32_t pulse_count_ = 0;
    volatile uint64_t last_isr_us_ = 0;
    uint32_t last_calc_ms_ = 0;
    float current_speed_ = 0.0F;
    float smoothed_speed_ = 0.0F;
    float last_speed_ = 0.0F;
    float filtered_pulse_ = 0.0F;
};

extern SpeedDrive CarDrive;
