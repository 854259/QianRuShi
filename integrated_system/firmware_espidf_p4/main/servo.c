#include "servo.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "SERVO_PWM";
#define SERVO_PIN_J1     /* 搴曞骇 */
#define SERVO_PIN_J2     /* 鑲╅儴 */
#define SERVO_PIN_J3     /* 鑲橀儴 */
#define SERVO_PIN_J4     /* 鑵曢儴淇话 */
#define SERVO_PIN_J5     /* 鑵曢儴缈昏浆 (棰勭暀) */
#define SERVO_PIN_J6     /* 澶圭埅/娓呮磥鍒?*/
/* 鎶婂紩鑴氭斁杩涙暟缁勶紝鏂逛究鐢?ID (1~6) 寰幆璋冪敤 */
static const int SERVO_PINS[SERVO_NUM_JOINTS] = {
    SERVO_PIN_J1, SERVO_PIN_J2, SERVO_PIN_J3,
    SERVO_PIN_J4, SERVO_PIN_J5, SERVO_PIN_J6
};
/* =======================================================
 * LEDC 纭欢鍙傛暟閰嶇疆
 * ======================================================= */
#define SERVO_LEDC_TIMER       LEDC_TIMER_0
#define SERVO_LEDC_MODE        LEDC_LOW_SPEED_MODE // ESP32 杈冩柊鑺墖缁熶竴浣跨敤浣庨€熸ā寮忓嵆鍙?
#define SERVO_LEDC_DUTY_RES    LEDC_TIMER_14_BIT   // 14浣嶅垎杈ㄧ巼锛屾妸鍛ㄦ湡鍒囨垚 16384 浠斤紝绮惧害鏋侀珮
#define SERVO_LEDC_FREQ_HZ     50                  // MG996 鏍囧噯棰戠巼锛?0Hz (鍛ㄦ湡 20ms = 20000us)
static uint32_t us_to_duty(int16_t pulse_width_us)
{
    /* 寮鸿閽充綅锛屼繚鎶よ埖鏈轰笉鎾炴瑙?*/
    if (pulse_width_us < 500) pulse_width_us = 500;
    if (pulse_width_us > 2500) pulse_width_us = 2500;

    return (uint32_t)((pulse_width_us * 16384) / 20000);
}
void servo_init(void)
{
    ESP_LOGI(TAG, "Initializing LEDC PWM for MG996 servos...");

    /* 1. 閰嶇疆瀹氭椂鍣?(璁惧畾 50Hz 棰戠巼) */
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = SERVO_LEDC_MODE,
        .timer_num        = SERVO_LEDC_TIMER,
        .duty_resolution  = SERVO_LEDC_DUTY_RES,
        .freq_hz          = SERVO_LEDC_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    /* 2. 閰嶇疆 6 涓嫭绔嬮€氶亾  */
    for (int i = 0; i < SERVO_NUM_JOINTS; i++) {
        ledc_channel_config_t ledc_channel = {
            .speed_mode     = SERVO_LEDC_MODE,
            .channel        = (ledc_channel_t)i, /* 閫氶亾 0~5 */
            .timer_sel      = SERVO_LEDC_TIMER,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = SERVO_PINS[i],
            .duty           = us_to_duty(1500),  /* 榛樿鐩存帴杈撳嚭 1500us 灞呬腑淇″彿锛?*/
            .hpoint         = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

        ESP_LOGI(TAG, "Servo ID %d -> GPIO %d mapped to Channel %d", i + 1, SERVO_PINS[i], i);
    }

    ESP_LOGI(TAG, "All servos initialized to 1500us (Center).");
}
void servo_write_pos(uint8_t id, int16_t pulse_width_us, uint16_t speed)
{
    /* 鎷︽埅闈炴硶 ID锛岄槻姝㈡暟缁勮秺鐣岀垎鐐?*/
    if (id < 1 || id > SERVO_NUM_JOINTS) return;

    /* ID 1~6 瀵瑰簲 Channel 0~5 */
    ledc_channel_t channel = (ledc_channel_t)(id - 1);

    /* 灏嗗井绉掕浆鎹㈡垚纭欢鑺墖鑳芥噦鐨勫崰绌烘瘮鏁板€?*/
    uint32_t duty = us_to_duty(pulse_width_us);

    /* 鐩存帴灏嗗崰绌烘瘮鍐欏叆纭欢瀵勫瓨鍣紝PWM 娉㈠舰鐬棿鏀瑰彉 */
    ledc_set_duty(SERVO_LEDC_MODE, channel, duty);
    ledc_update_duty(SERVO_LEDC_MODE, channel);
}