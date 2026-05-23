#ifndef AVOID_CONTROL_H
#define AVOID_CONTROL_H

#include "config.h"
#include "SpeedDrive.h"

class AvoidControl {
public:
    void begin();
    void run();
    void stop();
    bool isActive();

private:
    bool active;
    int  currentServoAngle;
    bool lastTurnLeft;  // 记录上次决策的转向方向，供 handleTurning 持续使用

    // ── 连续扫描状态机 
    // 舵机扫描序列：右(30°) → 中(90°) → 左(150°) → 中(90°) → 循环
    static const int SWEEP_ANGLES[4];   // 扫描角度序列
    int  sweepStep;                     // 当前序列位置 0-3
    unsigned long lastSweepTime;        // 上次更新扫描步骤的时间

    // 三方向最新距离（由连续扫描持续更新）
    int distLeft;     // 150° 方向距离
    int distCenter;   // 90°  方向距离
    int distRight;    // 30°  方向距离

    // ── 主行为状态机 
    enum AvoidState {
        STATE_FORWARD,   // 正常前进（舵机同时扫描）
        STATE_TURNING,   // 差速/原地转向
        STATE_BACKING,   // 后退脱困
        STATE_UTURN      // 原地掉头（三方皆堵）
    };
    AvoidState currentState;
    unsigned long stateStartTime;

    // 记录上次选择的转向方向，避免来回摇摆
    int lastTurnDir;  // +1=右 -1=左 0=未定

    // ── 私有方法 
    void servoWrite(int angle);
    int  getDistance();

    void updateSweep();          // 连续扫描更新（每次run()调用）
    void handleForward();
    void handleTurning();
    void handleBacking();
    void handleUTurn();

    // 根据三方向距离做决策，返回选定状态
    AvoidState decideTurnState();
};

extern AvoidControl Avoider;

#endif