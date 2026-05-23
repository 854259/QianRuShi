#include "servo.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "SERVO_PWM";
#define SERVO_PIN_J1     /* 底座 */
#define SERVO_PIN_J2     /* 肩部 */
#define SERVO_PIN_J3     /* 肘部 */
#define SERVO_PIN_J4     /* 腕部俯仰 */
#define SERVO_PIN_J5     /* 腕部翻转 (预留) */
#define SERVO_PIN_J6     /* 夹爪/清洁刷 */
/* 把引脚放进数组，方便用 ID (1~6) 循环调用 */
static const int SERVO_PINS[SERVO_NUM_JOINTS] = {
    SERVO_PIN_J1, SERVO_PIN_J2, SERVO_PIN_J3, 
    SERVO_PIN_J4, SERVO_PIN_J5, SERVO_PIN_J6
};
/* =======================================================
 * LEDC 硬件参数配置
 * ======================================================= */
#define SERVO_LEDC_TIMER       LEDC_TIMER_0
#define SERVO_LEDC_MODE        LEDC_LOW_SPEED_MODE // ESP32 较新芯片统一使用低速模式即可
#define SERVO_LEDC_DUTY_RES    LEDC_TIMER_14_BIT   // 14位分辨率，把周期切成 16384 份，精度极高
#define SERVO_LEDC_FREQ_HZ     50                  // MG996 标准频率：50Hz (周期 20ms = 20000us)
static uint32_t us_to_duty(int16_t pulse_width_us)
{
    /* 强行钳位，保护舵机不撞死角 */
    if (pulse_width_us < 500) pulse_width_us = 500;
    if (pulse_width_us > 2500) pulse_width_us = 2500;
    
    return (uint32_t)((pulse_width_us * 16384) / 20000);
}
void servo_init(void)
{
    ESP_LOGI(TAG, "Initializing LEDC PWM for MG996 servos...");

    /* 1. 配置定时器 (设定 50Hz 频率) */
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = SERVO_LEDC_MODE,
        .timer_num        = SERVO_LEDC_TIMER,
        .duty_resolution  = SERVO_LEDC_DUTY_RES,
        .freq_hz          = SERVO_LEDC_FREQ_HZ,  
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    /* 2. 配置 6 个独立通道  */
    for (int i = 0; i < SERVO_NUM_JOINTS; i++) {
        ledc_channel_config_t ledc_channel = {
            .speed_mode     = SERVO_LEDC_MODE,
            .channel        = (ledc_channel_t)i, /* 通道 0~5 */
            .timer_sel      = SERVO_LEDC_TIMER,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = SERVO_PINS[i],
            .duty           = us_to_duty(1500),  /* 默认直接输出 1500us 居中信号！ */
            .hpoint         = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
        
        ESP_LOGI(TAG, "Servo ID %d -> GPIO %d mapped to Channel %d", i + 1, SERVO_PINS[i], i);
    }
    
    ESP_LOGI(TAG, "All servos initialized to 1500us (Center).");
}
void servo_write_pos(uint8_t id, int16_t pulse_width_us, uint16_t speed)
{
    /* 拦截非法 ID，防止数组越界爆炸 */
    if (id < 1 || id > SERVO_NUM_JOINTS) return;
    
    /* ID 1~6 对应 Channel 0~5 */
    ledc_channel_t channel = (ledc_channel_t)(id - 1);
    
    /* 将微秒转换成硬件芯片能懂的占空比数值 */
    uint32_t duty = us_to_duty(pulse_width_us);

    /* 直接将占空比写入硬件寄存器，PWM 波形瞬间改变 */
    ledc_set_duty(SERVO_LEDC_MODE, channel, duty);
    ledc_update_duty(SERVO_LEDC_MODE, channel);
}