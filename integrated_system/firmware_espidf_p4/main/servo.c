#include "servo.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG = "SERVO_PCA9685";
#define I2C_MASTER_SCL_IO           7           /*!< 替换成你 PCB 上实际的 SCL 引脚 */
#define I2C_MASTER_SDA_IO           8           /*!< 替换成你 PCB 上实际的 SDA 引脚 */
#define I2C_MASTER_NUM              I2C_NUM_0   /*!< ESP32 的 I2C 端口号 */
#define I2C_MASTER_FREQ_HZ          100000      /*!< I2C 频率：400kHz (高速模式) */
#define I2C_MASTER_TX_BUF_DISABLE   0           /*!< I2C master 不需要 TX buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0           /*!< I2C master 不需要 RX buffer */
/* =======================================================
 * 驱动芯片配置 (假设是 PCA9685)
 * ======================================================= */
#define PCA9685_I2C_ADDR            0x40        /*!< PCA9685 默认 I2C 地址，根据 A0-A5 硬件接线可能不同 */
#define PCA9685_MODE1_REG           0x00        /*!< 模式寄存器 1 */
#define PCA9685_PRESCALE_REG        0xFE        /*!< 频率预分频器寄存器 */
#define PCA9685_LED0_ON_L_REG       0x06        /*!< 通道 0 输出寄存器起始地址 */

/* 你的 6 个舵机插在芯片的哪几个通道上？(0~15) */
static const uint8_t SERVO_CHANNELS[SERVO_NUM_JOINTS] = { 0, 1, 2, 3, 4, 5 };
/* 封装一个便捷的 I2C 写寄存器函数 */
static esp_err_t pca9685_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t write_buf[2] = { reg, data };
    return i2c_master_write_to_device(I2C_MASTER_NUM, PCA9685_ADDR, write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
}
void servo_init(void)
{
    ESP_LOGI(TAG, "Initializing I2C for PCA9685...");

    /* 1. 配置 ESP32 的 I2C 主机模式 */
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode,
                                       I2C_MASTER_RX_BUF_DISABLE,
                                       I2C_MASTER_TX_BUF_DISABLE, 0));
                                       /* 2. 初始化 PCA9685 芯片设置 50Hz 频率 */
    /* 计算公式: prescale = round(25MHz / (4096 * 50Hz)) - 1 = 121 */
    uint8_t prescale_val = 121;

    /* 设置流程: 必须先进入 Sleep 模式才能修改 PRESCALE 频率寄存器 */
    pca9685_write_reg(PCA9685_MODE1, 0x10);                 /* Sleep */
    vTaskDelay(pdMS_TO_TICKS(5));

    pca9685_write_reg(PCA9685_PRESCALE, prescale_val);      /* 设置频率为 50Hz */
    pca9685_write_reg(PCA9685_MODE1, 0x00);                 /* 唤醒芯片 (Wake) */
    vTaskDelay(pdMS_TO_TICKS(5));                           /* 等待振荡器稳定 */

    pca9685_write_reg(PCA9685_MODE1, 0xA0);                 /* 开启自动地址递增模式 (方便连续写入) */

    ESP_LOGI(TAG, "PCA9685 initialized. PWM Frequency set to ~50Hz.");void servo_write_pos(uint8_t id, int16_t pulse_width_us, uint16_t speed)
{
    if (id < 1 || id > SERVO_NUM_JOINTS) return;

    /* 安全钳位保护 (MG996物理极限) */
    if (pulse_width_us < 500) pulse_width_us = 500;
    if (pulse_width_us > 2500) pulse_width_us = 2500;

    uint8_t channel = SERVO_CHANNELS[id - 1];

    /* * 核心算法: PCA9685 精度为 12位 (0~4095)
     * 50Hz 周期 = 20ms = 20000us
     * 目标计数值 = (脉冲宽度 / 20000) * 4096
     */
    uint16_t off_count = (uint16_t)((pulse_width_us * 4096) / 20000);

    /* * 组装 I2C 数据包连续写入 4 个寄存器：
     * [寄存器首地址, ON_L, ON_H, OFF_L, OFF_H]
     * 我们让 ON 时间始终为 0 (一周期开始就拉高)，只调节 OFF 的时间点
     */
    uint8_t write_buf[5];
    write_buf[0] = PCA9685_LED0_ON_L + (4 * channel);
    write_buf[1] = 0;                         // ON_L
    write_buf[2] = 0;                         // ON_H
    write_buf[3] = off_count & 0xFF;          // OFF_L  (低8位)
    write_buf[4] = (off_count >> 8) & 0xFF;   // OFF_H  (高4位)

    /* 一次性将 4 个字节写入对应的通道寄存器 */
    i2c_master_write_to_device(I2C_MASTER_NUM, PCA9685_ADDR, write_buf, sizeof(write_buf), pdMS_TO_TICKS(10));
}
}