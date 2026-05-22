#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ================= 电机引脚定义 =================
#define MOTO_LF_A 25
#define MOTO_LF_B 26
#define MOTO_RF_A 13
#define MOTO_RF_B 12
#define MOTO_LR_A 32
#define MOTO_LR_B 33
#define MOTO_RR_A 14
#define MOTO_RR_B 27

// ================= 传感器引脚 =================
#define PIN_SPEED_SENSOR 34 

// 循迹模块 (5路)
#define PIN_TRACE_1 17  // 最左
#define PIN_TRACE_2 16
#define PIN_TRACE_3 4   // 中间
#define PIN_TRACE_4 2
#define PIN_TRACE_5 15  // 最右

// 超声波与舵机
#define SR04_TRIG 23
#define SR04_ECHO 22
#define SERVO_PIN 21

// 风扇控制
#define FAN_PIN 5

// ================= 速度计算参数 =================
#define CM_PER_PULSE 0.4045        // 轮径周长换算系数
#define EMA_ALPHA 0.3              // 滤波系数（降低以减少抖动）
#define SPEED_CALC_INTERVAL 100    // 速度计算周期(ms)
#define SPEED_THRESHOLD 2.0        // 速度变化阈值(cm/s)，小于此值视为稳定
#define MIN_PULSE_FOR_SPEED 2      // 最小脉冲数阈值

// ================= 电机控制参数 =================
#define SERVO_FREQ 50
#define MAX_PWM 255
#define PWM_FREQ 12000
#define PWM_RESOLUTION 8

// ================= 循迹参数 =================
#define TRACE_SPEED_FAST 240       // 直行速度
#define TRACE_SPEED_MEDIUM 200     // 中等转向速度
#define TRACE_SPEED_SLOW 160       // 慢速转向
#define TRACE_SPEED_TURN 180       // 原地转向速度
#define TRACE_SPEED_REVERSE 150    // 反向辅助速度

// ================= 避障参数 =================
#define AVOID_DISTANCE_EMERGENCY 20   // 紧急停止距离(cm)   
#define AVOID_DISTANCE_SLOW      35   // 减速/停车决策距离(cm) 
#define AVOID_DISTANCE_SAFE      55   // 安全/恢复前进距离(cm) 
#define AVOID_SPEED_NORMAL       200  // 正常行驶速度        
#define AVOID_SPEED_SLOW         150  // 慢速/后退速度       
#define AVOID_SPEED_TURN         170  // 原地转向速度（新增）
#define AVOID_SCAN_ANGLE_LEFT    150  // 左扫描角度
#define AVOID_SCAN_ANGLE_RIGHT   30   // 右扫描角度
#define AVOID_SCAN_DELAY         450  // 扫描等待(ms)        
#define AVOID_SWEEP_STEP_MS      400  // 连续扫描每步间隔(ms)（新增）
#define AVOID_BACK_MS            900  // 后退持续时间(ms)（新增）

#endif