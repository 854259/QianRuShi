#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

#define SERVO_NUM_JOINTS 6

/* 1. 初始化 ESP32 的 LEDC 硬件 PWM 模块 */
void servo_init(void);

/* 2. 写入目标脉宽 (单位：微秒 us，范围 500-2500) 
 * 注：普通的 MG996 是“全速疯狗”，软件发什么它就以最大物理速度冲过去，
 * 所以为了兼容上层的函数签名，我们保留 speed 参数，但在底层直接忽略它。
 */
void servo_write_pos(uint8_t id, int16_t pulse_width_us, uint16_t speed);

#endif