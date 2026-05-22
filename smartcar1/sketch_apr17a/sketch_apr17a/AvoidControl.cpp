#include "AvoidControl.h"

AvoidControl Avoider;

// 舵机扫描角度序列：右(30°)→中(90°)→左(150°)→中(90°)→循环
const int AvoidControl::SWEEP_ANGLES[4] = {30, 90, 150, 90};

// 初始化
void AvoidControl::begin() {
    pinMode(SR04_TRIG, OUTPUT);
    pinMode(SR04_ECHO, INPUT);

    bool ok = ledcAttach(SERVO_PIN, SERVO_FREQ, 16);
    Serial.println(ok ? "舵机PWM初始化成功" : "警告:舵机PWM初始化失败!");

    currentServoAngle = 90;
    servoWrite(90);

    active         = false;
    lastTurnLeft   = false;
    lastTurnDir    = 0;
    sweepStep      = 0;
    lastSweepTime  = 0;
    distLeft       = 200;
    distCenter     = 200;
    distRight      = 200;
    currentState   = STATE_FORWARD;
    stateStartTime = 0;
}

// 舵机写角度：按转动幅度动态等待，确保到位再测距
void AvoidControl::servoWrite(int angle) {
    angle = constrain(angle, 0, 180);
    int delta = abs(angle - currentServoAngle);
    currentServoAngle = angle;
    long duty = ((angle / 180.0) * 2000.0 + 500.0) * 65535.0 / 20000.0;
    ledcWrite(SERVO_PIN, duty);
    // 每度约3ms，最少80ms，最多500ms
    int waitMs = constrain(delta * 3, 80, 500);
    delay(waitMs);
}

// 超声波测距（25ms超时）
int AvoidControl::getDistance() {
    digitalWrite(SR04_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(SR04_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(SR04_TRIG, LOW);

    long dur = pulseIn(SR04_ECHO, HIGH, 25000);
    if (dur == 0) return 200;
    return constrain((int)(dur * 0.034 / 2), 0, 200);
}

// 连续扫描：每 AVOID_SWEEP_STEP_MS 推进一步，行进中持续更新三方向距离
// 节奏：转到目标角 → 等到位 → 测距 → 记录上一步角度的距离 → 推进
void AvoidControl::updateSweep() {
    unsigned long now = millis();
    if (now - lastSweepTime < AVOID_SWEEP_STEP_MS) return;

    // 转到当前步目标角度（servoWrite内已含动态延迟）
    servoWrite(SWEEP_ANGLES[sweepStep]);

    // 测距（舵机已到位）
    int d = getDistance();

    // 将测距结果归入对应方向
    int angle = SWEEP_ANGLES[sweepStep];
    if (angle <= 45) {
        distRight  = d;
    } else if (angle >= 135) {
        distLeft   = d;
    } else {
        distCenter = d;
    }

    Serial.printf("[扫描] 角度=%3d°  左=%3dcm 中=%3dcm 右=%3dcm\n",
                  angle, distLeft, distCenter, distRight);

    sweepStep     = (sweepStep + 1) % 4;
    lastSweepTime = now;
}

// 决策：三方向距离 → 选转向方向，记入 lastTurnLeft / lastTurnDir
// 返回下一个应切换的状态
AvoidControl::AvoidState AvoidControl::decideTurnState() {
    Serial.printf("[决策] 左=%dcm 中=%dcm 右=%dcm\n",
                  distLeft, distCenter, distRight);

    // 三方皆堵 → 后退
    if (distLeft  < AVOID_DISTANCE_SLOW &&
        distCenter < AVOID_DISTANCE_EMERGENCY &&
        distRight  < AVOID_DISTANCE_SLOW) {
        Serial.println("[决策] 三方受阻 → 后退");
        lastTurnDir = 0;
        return STATE_BACKING;
    }

    // 惯性加成：上次方向加10cm权重，防止来回摇摆
    int scoreLeft  = distLeft  + (lastTurnDir == -1 ? 10 : 0);
    int scoreRight = distRight + (lastTurnDir == +1 ? 10 : 0);

    if (scoreLeft >= scoreRight) {
        Serial.printf("[决策] 左转 (左%dcm 右%dcm)\n", distLeft, distRight);
        lastTurnLeft = true;
        lastTurnDir  = -1;
    } else {
        Serial.printf("[决策] 右转 (左%dcm 右%dcm)\n", distLeft, distRight);
        lastTurnLeft = false;
        lastTurnDir  = +1;
    }
    return STATE_TURNING;
}

// 状态：前进
// 四级距离策略：紧急停止 / 减速区停车决策 / 预警区慢速 / 安全全速
void AvoidControl::handleForward() {
    // 前进时舵机朝正前方，由 updateSweep 统一管理扫描
    int dist = distCenter; // 直接用连续扫描的中央距离，无需额外测距
    Serial.printf("[前进] 前方=%dcm\n", dist);

    if (dist < AVOID_DISTANCE_EMERGENCY) {
        CarDrive.stop();
        Serial.printf("[前进] 紧急停止 %dcm → 决策\n", dist);
        currentState   = decideTurnState();
        stateStartTime = millis();
    }
    else if (dist < AVOID_DISTANCE_SLOW) {
        // 减速区：停车，扫描已有数据直接决策
        CarDrive.stop();
        Serial.printf("[前进] 减速区 %dcm → 决策\n", dist);
        currentState   = decideTurnState();
        stateStartTime = millis();
    }
    else if (dist < AVOID_DISTANCE_SAFE) {
        // 预警区：慢速行驶，同时若侧方数据已有明显偏差则微调方向
        if (distLeft > distRight + 20) {
            CarDrive.run(AVOID_SPEED_SLOW - 20, AVOID_SPEED_SLOW); // 轻微左偏
        } else if (distRight > distLeft + 20) {
            CarDrive.run(AVOID_SPEED_SLOW, AVOID_SPEED_SLOW - 20); // 轻微右偏
        } else {
            CarDrive.run(AVOID_SPEED_SLOW, AVOID_SPEED_SLOW);
        }
    }
    else {
        CarDrive.run(AVOID_SPEED_NORMAL, AVOID_SPEED_NORMAL);
    }
}

// 状态：转向
// 持续发送转向指令，400ms后开始检测前方，超时重新决策
void AvoidControl::handleTurning() {
    unsigned long elapsed = millis() - stateStartTime;

    // 持续发送转向指令（两轮反向原地转）
    if (lastTurnLeft) {
        CarDrive.run(-AVOID_SPEED_TURN, AVOID_SPEED_TURN);
    } else {
        CarDrive.run(AVOID_SPEED_TURN, -AVOID_SPEED_TURN);
    }

    // 最短400ms内不检测，避免转向抖动
    if (elapsed < 400) return;

    // 用连续扫描的中央距离判断前方是否清空
    Serial.printf("[转向] 已转%lums 前方=%dcm\n", elapsed, distCenter);

    if (distCenter > AVOID_DISTANCE_SAFE) {
        CarDrive.stop();
        delay(80);
        Serial.println("[转向] 前方清空 → 前进");
        currentState   = STATE_FORWARD;
        stateStartTime = millis();
    }
    else if (elapsed > 1800) {
        CarDrive.stop();
        delay(80);
        Serial.println("[转向] 超时 → 重新决策");
        currentState   = decideTurnState();
        stateStartTime = millis();
    }
}

// 状态：后退
void AvoidControl::handleBacking() {
    unsigned long elapsed = millis() - stateStartTime;

    if (elapsed < AVOID_BACK_MS) {
        CarDrive.run(-AVOID_SPEED_SLOW, -AVOID_SPEED_SLOW);
    } else {
        CarDrive.stop();
        delay(80);
        Serial.println("[后退] 完成 → 原地掉头");
        // 后退完成后选净空大的一侧掉头
        lastTurnDir    = (distLeft >= distRight) ? -1 : +1;
        lastTurnLeft   = (lastTurnDir == -1);
        currentState   = STATE_UTURN;
        stateStartTime = millis();
    }
}

// 状态：原地掉头（三方受阻后退完再调头）
// 持续旋转直到前方清空，或超时再次后退
void AvoidControl::handleUTurn() {
    if (lastTurnDir == -1) {
        CarDrive.run(-AVOID_SPEED_TURN, AVOID_SPEED_TURN); // 原地左转
    } else {
        CarDrive.run(AVOID_SPEED_TURN, -AVOID_SPEED_TURN); // 原地右转
    }

    unsigned long elapsed = millis() - stateStartTime;

    if (elapsed > 600 && distCenter > AVOID_DISTANCE_SAFE) {
        CarDrive.stop();
        delay(80);
        Serial.println("[掉头] 前方清空 → 前进");
        currentState   = STATE_FORWARD;
        stateStartTime = millis();
    } else if (elapsed > 3000) {
        CarDrive.stop();
        Serial.println("[掉头] 超时 → 再次后退");
        currentState   = STATE_BACKING;
        stateStartTime = millis();
    }
}

// 主入口：每次由主循环调用
void AvoidControl::run() {
    if (!active) {
        active         = true;
        currentState   = STATE_FORWARD;
        sweepStep      = 0;
        lastSweepTime  = 0;
        distLeft = distCenter = distRight = 200;
        lastTurnDir    = 0;
        lastTurnLeft   = false;
        currentServoAngle = 90;
        servoWrite(90);
        Serial.println("[避障] 模式启动");
    }

    // ① 连续扫描（非阻塞，按AVOID_SWEEP_STEP_MS节奏推进）
    updateSweep();

    // ② 状态机驱动
    switch (currentState) {
        case STATE_FORWARD:  handleForward();  break;
        case STATE_TURNING:  handleTurning();  break;
        case STATE_BACKING:  handleBacking();  break;
        case STATE_UTURN:    handleUTurn();    break;
    }
}

// 停止避障模式
void AvoidControl::stop() {
    if (active) {
        CarDrive.stop();
        currentServoAngle = 90;
        servoWrite(90);
        active       = false;
        currentState = STATE_FORWARD;
        lastTurnDir  = 0;
        Serial.println("[避障] 模式停止");
    }
}

bool AvoidControl::isActive() {
    return active;
}