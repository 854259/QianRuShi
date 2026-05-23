#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "driver/ledc.h"

constexpr gpio_num_t SMARTCAR_MOTO_LF_A_GPIO = GPIO_NUM_7;
constexpr gpio_num_t SMARTCAR_MOTO_LF_B_GPIO = GPIO_NUM_8;
constexpr gpio_num_t SMARTCAR_MOTO_RF_A_GPIO = GPIO_NUM_23;
constexpr gpio_num_t SMARTCAR_MOTO_RF_B_GPIO = GPIO_NUM_21;
constexpr gpio_num_t SMARTCAR_MOTO_LR_A_GPIO = GPIO_NUM_22;
constexpr gpio_num_t SMARTCAR_MOTO_LR_B_GPIO = GPIO_NUM_20;
constexpr gpio_num_t SMARTCAR_MOTO_RR_A_GPIO = GPIO_NUM_6;
constexpr gpio_num_t SMARTCAR_MOTO_RR_B_GPIO = GPIO_NUM_5;

constexpr gpio_num_t SMARTCAR_SPEED_SENSOR_GPIO = GPIO_NUM_36;
constexpr gpio_num_t SMARTCAR_TRACE_GPIO[] = {
    GPIO_NUM_4,
    GPIO_NUM_3,
    GPIO_NUM_2,
    GPIO_NUM_32,
    GPIO_NUM_33,
};

constexpr gpio_num_t SMARTCAR_SR04_TRIG_GPIO = GPIO_NUM_26;
constexpr gpio_num_t SMARTCAR_SR04_ECHO_GPIO = GPIO_NUM_27;
constexpr gpio_num_t SMARTCAR_SERVO_GPIO = GPIO_NUM_48;
constexpr gpio_num_t SMARTCAR_FAN_GPIO = GPIO_NUM_53;
constexpr gpio_num_t SMARTCAR_STATUS_LED_GPIO = GPIO_NUM_46;

constexpr float SMARTCAR_CM_PER_PULSE = 0.4045F;
constexpr float SMARTCAR_EMA_ALPHA = 0.3F;
constexpr uint32_t SMARTCAR_SPEED_CALC_INTERVAL_MS = 100;
constexpr float SMARTCAR_SPEED_THRESHOLD_CM_S = 2.0F;
constexpr uint32_t SMARTCAR_MIN_PULSE_FOR_SPEED = 2;

constexpr uint32_t SMARTCAR_MOTOR_PWM_FREQ_HZ = 12000;
constexpr ledc_timer_bit_t SMARTCAR_MOTOR_PWM_RESOLUTION = LEDC_TIMER_8_BIT;
constexpr int SMARTCAR_MAX_PWM = 255;

constexpr int SMARTCAR_TRACE_SPEED_FAST = 240;
constexpr int SMARTCAR_TRACE_SPEED_MEDIUM = 200;
constexpr int SMARTCAR_TRACE_SPEED_SLOW = 160;
constexpr int SMARTCAR_TRACE_SPEED_TURN = 180;
constexpr int SMARTCAR_TRACE_SPEED_REVERSE = 150;

constexpr int SMARTCAR_AVOID_DISTANCE_EMERGENCY_CM = 20;
constexpr int SMARTCAR_AVOID_DISTANCE_SLOW_CM = 35;
constexpr int SMARTCAR_AVOID_DISTANCE_SAFE_CM = 55;
constexpr int SMARTCAR_AVOID_SPEED_NORMAL = 200;
constexpr int SMARTCAR_AVOID_SPEED_SLOW = 150;
constexpr int SMARTCAR_AVOID_SPEED_TURN = 170;
constexpr uint32_t SMARTCAR_AVOID_SWEEP_STEP_MS = 400;
constexpr uint32_t SMARTCAR_AVOID_BACK_MS = 900;

constexpr char SMARTCAR_AP_SSID[] = "ESP32_SmartCar";
constexpr char SMARTCAR_AP_PASSWORD[] = "12345678";
