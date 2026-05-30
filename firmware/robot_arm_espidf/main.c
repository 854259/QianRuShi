#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "servo.h"
#include "vision_api.h"
#include "robot_core.h"
#include "kinematics.h"
static const char *TAG = "MAIN";
#include <string.h>

/* ============ 串口调试模拟 AI 任务 ============ */
static void pc_command_task(void *arg)
{
    char rx_buf[128];
    ESP_LOGI("PC_CMD", "========================================");
    ESP_LOGI("PC_CMD", "PC Simulator Ready!");
    ESP_LOGI("PC_CMD", "Please input data in format: X Y Z V");
    ESP_LOGI("PC_CMD", "Example: 200 0 150 1  (V=1 means valid)");
    ESP_LOGI("PC_CMD", "========================================");

    while (1) {
        /* fgets 会阻塞等待串口输入（以回车 \n 结尾） */
        if (fgets(rx_buf, sizeof(rx_buf), stdin) != NULL) {
            vision_target_t target;
            int valid_flag = 0;

            /* 利用 sscanf 解析空格分隔的四个数字 */
            if (sscanf(rx_buf, "%f %f %f %d", &target.x, &target.y, &target.z, &valid_flag) == 4) {
                target.is_valid = (valid_flag != 0);

                /* 写入信箱 */
                vision_api_set_latest(&target);

                ESP_LOGI("PC_CMD", "=> [Mock AI] Injected: X=%.1f, Y=%.1f, Z=%.1f, Valid=%d",
                         target.x, target.y, target.z, target.is_valid);
            } else {
                ESP_LOGE("PC_CMD", "Invalid format! Use: X Y Z V");
            }
        }
        /* 稍微让出 CPU */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
void app_main(void)
{
    ESP_LOGI(TAG, "=== AI Vision Robot Arm Starting ===");

    /* ---------------------------------------------------------
     * 1. 硬件外设初始化层 (底层)
     * ---------------------------------------------------------*/

    /* 1.1 初始化舵机 UART 通信 (保留原代码) */
    ESP_LOGI(TAG, "Initializing servos...");
    servo_init();
    vTaskDelay(pdMS_TO_TICKS(500)); /* 必须保留：等待舵机上电稳定，不要急着发指令 */

    /* 1.2 【新增】初始化 AI 摄像头通信接口 */
    /* 比如配置一个新的 UART 或者 SPI 接口，专门用来听摄像头说话 */
    ESP_LOGI(TAG, "Initializing AI Vision Receiver...");
    vision_api_init();
    /* ---------------------------------------------------------
     * 2. 算法与逻辑状态初始化层 (中层)
     * ---------------------------------------------------------*/

    /* 2.1 初始化运动学参数 (保留原代码) */
    /* 这里会加载 kinematics.h 里的 DH 参数，并打印最大臂展 */
    ESP_LOGI(TAG, "Initializing kinematics...");
    kin_init();

    /* 2.2 【重写】极简版机械臂控制初始化 */
    /* 功能：给各个关节上锁（开启扭矩），并让机械臂缓慢移动到一个安全的初始原点 */
    ESP_LOGI(TAG, "Initializing robot core...");
    robot_core_init();
    /* ---------------------------------------------------------
     * 3. 启动实时操作系统任务 (顶层)
     * ---------------------------------------------------------*/

    /* 3.1 【重写】启动核心追踪线程 */
    /* 启动一个 50Hz (20ms) 的死循环任务：拿视觉坐标 -> 算逆运动学 -> 驱动舵机 */
    ESP_LOGI(TAG, "Starting main tracking task...");
    robot_core_start_task();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "System Ready! Waiting for AI camera data...");
    ESP_LOGI(TAG, "========================================");
    /* 3.2 启动电脑串口监听任务 */
    xTaskCreate(pc_command_task, "pc_cmd_task", 4096, NULL, 4, NULL);