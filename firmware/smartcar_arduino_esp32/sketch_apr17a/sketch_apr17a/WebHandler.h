#ifndef WEB_HANDLER_H
#define WEB_HANDLER_H

#include <WiFi.h>
#include <WebServer.h>
#include "config.h"

// 系统运行模式
enum SysMode {
    MODE_MANUAL,    // 手动模式
    MODE_TRACE,     // 循迹模式
    MODE_AVOID      // 避障模式
};

class WebHandler {
public:
    WebHandler();
    void begin(const char* ssid, const char* pass);
    void loop();

    // 模式管理
    SysMode getMode();
    void setMode(SysMode mode);

    // 风扇控制
    void setFanState(bool state);
    bool getFanState();

    // 日志功能
    void addLog(String msg);

private:
    WebServer server;
    SysMode currentMode;
    bool fanState;  // 风扇状态
    const char* apSsid;
    const char* apPassword;

    // 速度数据历史 (用于图表显示)
    static const int MAX_HISTORY = 50;
    float speedHistory[MAX_HISTORY];
    int historyIndex;
    unsigned long lastSpeedUpdate;

    // 最近有效速度记录（用于复制功能）
    static const int VALID_SPEED_COUNT = 5;
    float validSpeeds[VALID_SPEED_COUNT];
    int validSpeedIndex;

    // 日志缓冲
    String logBuffer;
    unsigned long lastLogTime;

    void handleRoot();
    void handleCmd();
    void handleData();
    void handleSpeedHistory();

    void updateSpeedHistory(float speed);
    void updateValidSpeed(float speed);  // 更新有效速度记录
};

extern WebHandler WebSys;

#endif