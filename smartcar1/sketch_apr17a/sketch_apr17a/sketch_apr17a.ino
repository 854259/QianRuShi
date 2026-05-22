/**************************************************************************************
 * 文件: /SmartCar_System/SmartCar_System.ino 
 * 平台: ESP32 (经典款 WROOM-32)
 * 作者：金康 & 零知实验室
 * 时间: 2026-3-7 (修复版)
 * 说明: ESP32 智能小车控制系统 - 状态机调度增强版
 * * 【重要提醒】
 * 1. 本代码已加入模式切换逻辑，支持网页切换：手动、循迹、避障。
 * 2. 如果串口报错 OTA partition invalid，请在 IDE 菜单：工具 -> Partition Scheme 中选择 "Huge APP"。
 * 3. 务必确认 WebHandler.cpp 中已使用 server.send_P 发送网页，否则会爆内存。
 ***************************************************************************************/

#include "config.h"
#include "SpeedDrive.h"
#include "TraceControl.h"
#include "AvoidControl.h"
#include "WebHandler.h"

// AP模式热点配置
const char* AP_SSID = "ESP32_SmartCar";
const char* AP_PASSWORD = "12345678";

// 系统运行状态打印
unsigned long lastStatusUpdate = 0;
const unsigned long STATUS_INTERVAL = 2000; // 每2秒打印一次状态

void setup() {
    // 1. 初始化串口通讯
    Serial.begin(115200);
    delay(3000); // 等待串口监视器开启
    
    Serial.println("\n" + String(50, '='));
    Serial.println("  🚗 阿勒泰天文台巡检机器人 - 底层验证程序");
    Serial.println("  🔧 硬件平台: ESP32-WROOM-32 (经典款)");
    Serial.println(String(50, '='));
    
    // 2. 打印硬件资源快照
    Serial.println("📊 硬件自检：");
    Serial.printf("  芯片型号: %s\n", ESP.getChipModel());
    Serial.printf("  CPU 频率: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("  Flash 大小: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("  空闲 Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.println("");
    
    // 3. 初始化所有底层驱动模块
    Serial.println("🔄 正在装载驱动模块...");
    
    Serial.print("  [1/4] 电机驱动与测速系统...");
    CarDrive.begin();
    delay(100);
    Serial.println(" OK");
    
    Serial.print("  [2/4] 红外循迹传感器逻辑...");
    Tracer.begin();
    delay(100);
    Serial.println(" OK");
    
    Serial.print("  [3/4] 超声波与避障舵机系统...");
    Avoider.begin();
    delay(100);
    Serial.println(" OK");
    
    Serial.print("  [4/4] 离线网页服务器 (AP模式)...");
    WebSys.begin(AP_SSID, AP_PASSWORD);
    delay(100);
    Serial.println(" OK");
    
    // 4. 强制复位，确保电机处于停止状态
    CarDrive.stop();
    
    // 5. 视觉提示：LED闪烁3次表示初始化成功
    pinMode(2, OUTPUT);
    for (int i = 0; i < 3; i++) {
        digitalWrite(2, HIGH);
        delay(150);
        digitalWrite(2, LOW);
        delay(150);
    }
    
    Serial.println("\n✅ 系统就绪！手机连接热点并访问 http://192.168.4.1 开始遥控。");
    Serial.println(String(50, '=') + "\n");
}

void loop() {
    // 1. 处理 Web 请求：响应手机端按钮按下、模式切换等
    WebSys.loop();
    
    // 2. 电机测速后台循环：计算当前的实时速度 cm/s
    CarDrive.loop();
    
    // 3. 【核心修复】主控状态机：根据网页选择的模式执行不同算法
    SysMode currentMode = WebSys.getMode();
    
    switch (currentMode) {
        case MODE_MANUAL:
            // 手动模式：控制逻辑由 WebHandler.cpp 接收指令后直接调用 CarDrive 执行
            break;
            
        case MODE_TRACE:
            // 自动循迹模式：激活红外传感器逻辑
            Tracer.run();
            break;
            
        case MODE_AVOID:
            // 智能避障模式：激活舵机扫描与超声波测距逻辑
            Avoider.run();
            break;
            
        default:
            CarDrive.stop();
            break;
    }
    
    // 4. 定时监控系统健康度
    unsigned long now = millis();
    if (now - lastStatusUpdate >= STATUS_INTERVAL) {  
        Serial.printf("[运行中] Heap: %d bytes | 当前模式: %s\n", 
                      ESP.getFreeHeap(), 
                      (currentMode == MODE_MANUAL ? "手动" : (currentMode == MODE_TRACE ? "循迹" : "避障")));
        lastStatusUpdate = now;
    }
    
    // 5. 核心保活：释放 CPU 时间片，防止触发看门狗重启
    yield();
    delay(1);
}