#ifndef TRACE_CONTROL_H
#define TRACE_CONTROL_H

#include "config.h"
#include "SpeedDrive.h"

class TraceControl {
public:
    void begin();
    void run();           // 执行循迹逻辑
    void stop();          // 停止循迹
    bool isActive();      // 是否处于活跃状态

private:
    bool active;
    unsigned long lastUpdateTime;

    // 传感器读取
    struct SensorData {
        bool s1, s2, s3, s4, s5;  // 5路传感器状态
    };

    SensorData readSensors();
    void executeAction(const SensorData& sensors);
};

extern TraceControl Tracer;

#endif