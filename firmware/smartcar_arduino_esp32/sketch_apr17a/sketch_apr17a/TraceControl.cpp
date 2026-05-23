#include "TraceControl.h"

TraceControl Tracer;

void TraceControl::begin() {
    // 初始化循迹传感器引脚
    pinMode(PIN_TRACE_1, INPUT);
    pinMode(PIN_TRACE_2, INPUT);
    pinMode(PIN_TRACE_3, INPUT);
    pinMode(PIN_TRACE_4, INPUT);
    pinMode(PIN_TRACE_5, INPUT);
    
    active = false;
    lastUpdateTime = 0;
}

TraceControl::SensorData TraceControl::readSensors() {
    SensorData data;
    // 假设黑底白线，传感器检测到白线为 HIGH (1)
    // 如果是白底黑线，请在这里取反
    data.s1 = !digitalRead(PIN_TRACE_1);
    data.s2 = !digitalRead(PIN_TRACE_2);
    data.s3 = !digitalRead(PIN_TRACE_3);
    data.s4 = !digitalRead(PIN_TRACE_4);
    data.s5 = !digitalRead(PIN_TRACE_5);
    return data;
}

void TraceControl::executeAction(const SensorData& sensors) {
    // 计算传感器激活数量
    int activeCount = sensors.s1 + sensors.s2 + sensors.s3 + sensors.s4 + sensors.s5;
    
    // 1. 十字路口或宽线检测 (4个或以上传感器触发)
    if (activeCount >= 4) {
        // 十字路口策略：减速直行
        CarDrive.run(TRACE_SPEED_SLOW, TRACE_SPEED_SLOW);
        Serial.println("[循迹] 十字路口 - 减速通过");
        return;
    }
    
    // 2. 完全丢线 (全黑)
    if (activeCount == 0) {
        // 停止并尝试小幅度搜索
        CarDrive.stop();
        Serial.println("[循迹] 丢线 - 停止");
        return;
    }

    // 3. 精细化差速控制 - 基于传感器组合
    
    // 完美居中 - 只有中间传感器触发
    if (!sensors.s1 && !sensors.s2 && sensors.s3 && !sensors.s4 && !sensors.s5) {
        CarDrive.run(TRACE_SPEED_FAST, TRACE_SPEED_FAST);
        return;
    }
    
    // 轻微左偏 - s2和s3
    if (!sensors.s1 && sensors.s2 && sensors.s3 && !sensors.s4 && !sensors.s5) {
        CarDrive.run(TRACE_SPEED_MEDIUM, TRACE_SPEED_FAST);
        return;
    }
    
    // 轻微右偏 - s3和s4
    if (!sensors.s1 && !sensors.s2 && sensors.s3 && sensors.s4 && !sensors.s5) {
        CarDrive.run(TRACE_SPEED_FAST, TRACE_SPEED_MEDIUM);
        return;
    }
    
    // 中度左偏 - 只有s2
    if (!sensors.s1 && sensors.s2 && !sensors.s3 && !sensors.s4 && !sensors.s5) {
        CarDrive.run(TRACE_SPEED_SLOW, TRACE_SPEED_FAST);
        return;
    }
    
    // 中度右偏 - 只有s4
    if (!sensors.s1 && !sensors.s2 && !sensors.s3 && sensors.s4 && !sensors.s5) {
        CarDrive.run(TRACE_SPEED_FAST, TRACE_SPEED_SLOW);
        return;
    }
    
    // 严重左偏 - s1或s1+s2
    if (sensors.s1) {
        // 原地左转 + 反向辅助
        CarDrive.run(-TRACE_SPEED_REVERSE, TRACE_SPEED_TURN);
        return;
    }
    
    // 严重右偏 - s5或s4+s5
    if (sensors.s5) {
        // 原地右转 + 反向辅助
        CarDrive.run(TRACE_SPEED_TURN, -TRACE_SPEED_REVERSE);
        return;
    }
    
    // 其他组合 - 根据中心偏移决策
    if (sensors.s2 || sensors.s3) {
        // 偏左
        CarDrive.run(TRACE_SPEED_SLOW, TRACE_SPEED_MEDIUM);
    } else {
        // 偏右
        CarDrive.run(TRACE_SPEED_MEDIUM, TRACE_SPEED_SLOW);
    }
}

void TraceControl::run() {
    if (!active) {
        active = true;
        Serial.println("[循迹] 模式启动");
    }
    
    // 读取传感器
    SensorData sensors = readSensors();
    
    // 执行动作
    executeAction(sensors);
    
    lastUpdateTime = millis();
}

void TraceControl::stop() {
    if (active) {
        CarDrive.stop();
        active = false;
        Serial.println("[循迹] 模式停止");
    }
}

bool TraceControl::isActive() {
    return active;
}