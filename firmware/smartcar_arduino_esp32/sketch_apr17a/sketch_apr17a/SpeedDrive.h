#ifndef SPEED_DRIVE_H
#define SPEED_DRIVE_H

#include "config.h"

class SpeedDrive {
public:
    void begin();
    void loop(); // 在主循环调用，用于计算速度

    // 电机控制
    void stop();
    void run(int speedL, int speedR);

    // 速度获取
    float getSpeed();              // 获取当前速度 cm/s
    float getSmoothedSpeed();      // 获取平滑后的速度（用于显示）
    bool isSpeedStable();          // 判断速度是否稳定

private:
    // 测速相关变量
    volatile unsigned long pulseCount;
    unsigned long lastIsrTime;
    float currentSpeed;
    float smoothedSpeed;           // 用于显示的平滑速度
    float lastSpeed;               // 上次速度，用于稳定性判断
    float filteredPulse;
    unsigned long lastCalcTime;

    // 硬件相关
    static void IRAM_ATTR isrHandler(void* arg);
    void setMotorGroup(int pinA1, int pinB1, int pinA2, int pinB2, int speed);
};

extern SpeedDrive CarDrive;

#endif