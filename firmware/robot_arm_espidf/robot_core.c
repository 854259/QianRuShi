#include "robot_core.h"
#include "servo.h"
#include "kinematics.h"
#include "vision_api.h"   /* 刚才我们写的信箱接口 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ROBOT_CORE";
/* ============ 硬件安全：关节限位配置 ============
 * 这是防止机械臂自杀的最后一道防线，保留原代码的结构
 */
typedef struct {
    uint8_t  id;
    int16_t  min_pos;
    int16_t  max_pos;
    int16_t  home_pos;
} joint_config_t;

static const joint_config_t JOINTS[SERVO_NUM_JOINTS] = {
    { .id = 1, .min_pos = 500, .max_pos = 2500, .home_pos = 1500 }, /* 底座 */
    { .id = 2, .min_pos = 500, .max_pos = 2500, .home_pos = 1500 }, /* 肩部 */
    { .id = 3, .min_pos = 500, .max_pos = 2500, .home_pos = 1500 }, /* 肘部 */
    { .id = 4, .min_pos = 500,  .max_pos = 2500, .home_pos = 1500 }, /* 腕部俯仰 */
    { .id = 5, .min_pos = 500, .max_pos = 2500, .home_pos = 1500 }, /* 腕部翻转 (不参与坐标) */
    { .id = 6, .min_pos = 700, .max_pos = 2300, .home_pos = 1800 }, /* 夹爪/清洁刷 */
};
/* 安全钳位函数 */
static int16_t clamp(int16_t val, int16_t lo, int16_t hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}
/* 一次性向底层发送6个舵机的数据 */
static void write_all_positions(const int16_t pos[SERVO_NUM_JOINTS]) {
    for (int i = 0; i < SERVO_NUM_JOINTS; i++) {
        int16_t clamped = clamp(pos[i], JOINTS[i].min_pos, JOINTS[i].max_pos);
        /* 速度设为 500 (中等速度)，防止动作过猛 */
        servo_write_pos(JOINTS[i].id, clamped, 500);
    }
}
/* ============ 核心线程：50Hz 视觉追踪 ============ */
static void robot_core_task(void *arg)
{
    vision_target_t target;
    kin_pos_t       target_pos;
    kin_joints_t    target_joints;
    int16_t         servo_cmd[SERVO_NUM_JOINTS];

    /* 期望的末端腕部俯仰角 (弧度)。如果是清洁镜面，可能需要保持刷子与镜面垂直 */
    float desired_psi = 0.0f;

    while (1) {
        /* 1. 从信箱里“拉”取最新的视觉目标 */
        bool has_target = vision_api_get_latest(&target);

        if (has_target && target.is_valid) {

            /* 【预留口子】这里以后可以加上手眼标定的矩阵平移算法
             * 比如： target_pos.x = target.x + 摄像头X轴偏移量;
             */
            target_pos.x = target.x;
            target_pos.y = target.y;
            target_pos.z = target.z;

            /* 2. 空气墙检查：目标是否在机械臂物理臂展范围内？ */
            if (kin_check_workspace(&target_pos)) {

                /* 3. 核心计算：调用逆运动学解算关节角度 (弧度) */
                kin_result_t ik_res = kin_inverse(&target_pos, desired_psi, &target_joints);

                if (ik_res == KIN_OK) {
                    /* 4. 单位转换：弧度 -> 0-4095 的舵机步数 */
                    servo_cmd[0] = kin_rad_to_pos(0, target_joints.j[0]);
                    servo_cmd[1] = kin_rad_to_pos(1, target_joints.j[1]);
                    servo_cmd[2] = kin_rad_to_pos(2, target_joints.j[2]);
                    servo_cmd[3] = kin_rad_to_pos(3, target_joints.j[3]);

                    /* 5. 腕部翻转(J5)和夹爪(J6) 根据业务需求单独控制，这里先保持Home位 */
                    servo_cmd[4] = JOINTS[4].home_pos;
                    servo_cmd[5] = JOINTS[5].home_pos; // 以后如果是刷子，就在这里给力

                    /* 6. 发送指令给底层串口驱动 */
                    write_all_positions(servo_cmd);
                }
             }
            }else {
                    ESP_LOGW(TAG, "IK Failed: Target Unreachable or Singular");
                }
        /* 严格控制刷新率：20ms (50Hz) */
        vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
/* ============ 公共初始化接口 ============ */
void robot_core_init(void)
{
    ESP_LOGI(TAG, "Robot Core Init... Turning on torques.");

    int16_t home_cmd[SERVO_NUM_JOINTS];

    /* 1. 开启所有舵机的扭矩锁 */
    for (int i = 0; i < SERVO_NUM_JOINTS; i++) {
        home_cmd[i] = JOINTS[i].home_pos;
    }

    /* 2. 让机械臂缓慢回到安全的零点位置 */
    ESP_LOGI(TAG, "Moving to HOME position...");
    write_all_positions(home_cmd);

    /* 3. 等待它走到位 */
    vTaskDelay(pdMS_TO_TICKS(1500));
}
/* ============ 启动核心追踪线程 ============ */
void robot_core_start_task(void)
{
    /* 创建 FreeRTOS 任务
     * 参数依次为：任务函数、任务名称、栈空间(4096字节防浮点溢出)、传递参数、优先级、任务句柄
     */
    xTaskCreate(robot_core_task, "robot_core_task", 4096, NULL, 5, NULL);
}