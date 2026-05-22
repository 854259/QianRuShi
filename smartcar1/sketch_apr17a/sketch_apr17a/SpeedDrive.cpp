#include "SpeedDrive.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
SpeedDrive CarDrive;

// 中断处理函数
void IRAM_ATTR SpeedDrive::isrHandler(void* arg) {
    SpeedDrive* obj = (SpeedDrive*)arg;
    unsigned long now = micros();
    // 1ms 去抖动
    if (now - obj->lastIsrTime > 1000) {
        obj->pulseCount++;
        obj->lastIsrTime = now;
    }
}

void SpeedDrive::begin() {

    // 1. 初始化电机引脚
    pinMode(MOTO_LF_A, OUTPUT); pinMode(MOTO_LF_B, OUTPUT);
    pinMode(MOTO_RF_A, OUTPUT); pinMode(MOTO_RF_B, OUTPUT);
    pinMode(MOTO_LR_A, OUTPUT); pinMode(MOTO_LR_B, OUTPUT);
    pinMode(MOTO_RR_A, OUTPUT); pinMode(MOTO_RR_B, OUTPUT);

    // 2. 配置 PWM 通道 (0-7)
    // 格式：ledcAttach(引脚, 频率, 分辨率)，返回是否成功
    bool pwmInitOK = true;
    pwmInitOK &= ledcAttach(MOTO_LF_A, PWM_FREQ, PWM_RESOLUTION);
    pwmInitOK &= ledcAttach(MOTO_LF_B, PWM_FREQ, PWM_RESOLUTION);
    pwmInitOK &= ledcAttach(MOTO_RF_A, PWM_FREQ, PWM_RESOLUTION);
    pwmInitOK &= ledcAttach(MOTO_RF_B, PWM_FREQ, PWM_RESOLUTION);
    pwmInitOK &= ledcAttach(MOTO_LR_A, PWM_FREQ, PWM_RESOLUTION);
    pwmInitOK &= ledcAttach(MOTO_LR_B, PWM_FREQ, PWM_RESOLUTION);
    pwmInitOK &= ledcAttach(MOTO_RR_A, PWM_FREQ, PWM_RESOLUTION);
    pwmInitOK &= ledcAttach(MOTO_RR_B, PWM_FREQ, PWM_RESOLUTION);

    // 打印初始化结果
    if(pwmInitOK){
      Serial.println("PWM初始化成功");
    } else{
      Serial.println("警告:部分PWM通道初始化失败!");
    }

    // 3. 初始化测速中断
    pinMode(PIN_SPEED_SENSOR, INPUT); 
    pulseCount = 0;
    lastIsrTime = 0;
    currentSpeed = 0;
    smoothedSpeed = 0;
    lastSpeed = 0;
    filteredPulse = 0;
    lastCalcTime = millis();
    
    attachInterruptArg(digitalPinToInterrupt(PIN_SPEED_SENSOR), isrHandler, this, RISING);
}

// 电机控制：按引脚操作PWM（核心修改）
void SpeedDrive::setMotorGroup(int pinA1, int pinB1, int pinA2, int pinB2, int speed) {
    int pwmVal = abs(speed);
    pwmVal = constrain(pwmVal, 0, MAX_PWM);

    if (speed > 0) {
        // 前进：A引脚输出PWM，B引脚关闭
        ledcWrite(pinA1, pwmVal);  // 直接用引脚控制，替代原通道控制
        ledcWrite(pinB1, 0);
        ledcWrite(pinA2, pwmVal);
        ledcWrite(pinB2, 0);
    } else if (speed < 0) {
        // 后退：B引脚输出PWM，A引脚关闭
        ledcWrite(pinA1, 0);
        ledcWrite(pinB1, pwmVal);
        ledcWrite(pinA2, 0);
        ledcWrite(pinB2, pwmVal);
    } else {
        // 停止：所有引脚关闭
        ledcWrite(pinA1, 0);
        ledcWrite(pinB1, 0);
        ledcWrite(pinA2, 0);
        ledcWrite(pinB2, 0);
    }
}

void SpeedDrive::run(int speedL, int speedR) {
    // 限制速度范围
    speedL = constrain(speedL, -MAX_PWM, MAX_PWM);
    speedR = constrain(speedR, -MAX_PWM, MAX_PWM);
    
    // 控制左侧前后轮（直接传入引脚宏，替代原通道号）
    setMotorGroup(MOTO_LF_A, MOTO_LF_B, MOTO_LR_A, MOTO_LR_B, speedL);
    
    // 控制右侧前后轮
    setMotorGroup(MOTO_RF_A, MOTO_RF_B, MOTO_RR_A, MOTO_RR_B, speedR);
}

void SpeedDrive::stop() {
    // 遍历所有电机引脚，关闭PWM输出
    ledcWrite(MOTO_LF_A, 0);
    ledcWrite(MOTO_LF_B, 0);
    ledcWrite(MOTO_RF_A, 0);
    ledcWrite(MOTO_RF_B, 0);
    ledcWrite(MOTO_LR_A, 0);
    ledcWrite(MOTO_LR_B, 0);
    ledcWrite(MOTO_RR_A, 0);
    ledcWrite(MOTO_RR_B, 0);
    
    // 清零速度相关变量（不变）
    currentSpeed = 0;
    smoothedSpeed = 0;
}

void SpeedDrive::loop() {
    unsigned long now = millis();
    
    // 按设定周期计算速度
    if (now - lastCalcTime >= SPEED_CALC_INTERVAL) {
        noInterrupts();
        unsigned long raw = pulseCount;
        pulseCount = 0;
        interrupts();

        // 只有足够的脉冲才计算速度，减少低速抖动
        if (raw >= MIN_PULSE_FOR_SPEED) {
            // EMA 滤波算法
            filteredPulse = (EMA_ALPHA * raw) + ((1.0 - EMA_ALPHA) * filteredPulse);
            
            // 计算速度 cm/s (脉冲数 * 10 得到每秒脉冲数)
            float newSpeed = (filteredPulse * (1000.0 / SPEED_CALC_INTERVAL)) * CM_PER_PULSE;
            
            // 二次平滑用于显示
            smoothedSpeed = (0.7 * newSpeed) + (0.3 * smoothedSpeed);
            
            // 更新当前速度
            currentSpeed = newSpeed;
        } else {
            // 脉冲太少，可能已停止
            filteredPulse *= 0.5; // 逐渐衰减
            currentSpeed = filteredPulse * (1000.0 / SPEED_CALC_INTERVAL) * CM_PER_PULSE;
            smoothedSpeed *= 0.7;
        }
        
        lastCalcTime = now;
    }
}

float SpeedDrive::getSpeed() { 
    return currentSpeed; 
}

float SpeedDrive::getSmoothedSpeed() { 
    return smoothedSpeed; 
}

bool SpeedDrive::isSpeedStable() {
    float diff = abs(currentSpeed - lastSpeed);
    lastSpeed = currentSpeed;
    return (diff < SPEED_THRESHOLD);
}