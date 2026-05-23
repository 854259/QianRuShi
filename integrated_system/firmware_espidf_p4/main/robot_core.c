#include "robot_core.h"
#include "servo.h"
#include "kinematics.h"
#include "vision_api.h"   /* 鍒氭墠鎴戜滑鍐欑殑淇＄鎺ュ彛 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ROBOT_CORE";
/* ============ 纭欢瀹夊叏锛氬叧鑺傞檺浣嶉厤缃?============
 * 杩欐槸闃叉鏈烘鑷傝嚜鏉€鐨勬渶鍚庝竴閬撻槻绾匡紝淇濈暀鍘熶唬鐮佺殑缁撴瀯
 */
typedef struct {
    uint8_t  id;
    int16_t  min_pos;
    int16_t  max_pos;
    int16_t  home_pos;
} joint_config_t;

static const joint_config_t JOINTS[SERVO_NUM_JOINTS] = {
    { .id = 1, .min_pos = 500, .max_pos = 2500, .home_pos = 1500 }, /* 搴曞骇 */
    { .id = 2, .min_pos = 500, .max_pos = 2500, .home_pos = 1500 }, /* 鑲╅儴 */
    { .id = 3, .min_pos = 500, .max_pos = 2500, .home_pos = 1500 }, /* 鑲橀儴 */
    { .id = 4, .min_pos = 500,  .max_pos = 2500, .home_pos = 1500 }, /* 鑵曢儴淇话 */
    { .id = 5, .min_pos = 500, .max_pos = 2500, .home_pos = 1500 }, /* 鑵曢儴缈昏浆 (涓嶅弬涓庡潗鏍? */
    { .id = 6, .min_pos = 700, .max_pos = 2300, .home_pos = 1800 }, /* 澶圭埅/娓呮磥鍒?*/
};
/* 瀹夊叏閽充綅鍑芥暟 */
static int16_t clamp(int16_t val, int16_t lo, int16_t hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}
/* 涓€娆℃€у悜搴曞眰鍙戦€?涓埖鏈虹殑鏁版嵁 */
static void write_all_positions(const int16_t pos[SERVO_NUM_JOINTS]) {
    for (int i = 0; i < SERVO_NUM_JOINTS; i++) {
        int16_t clamped = clamp(pos[i], JOINTS[i].min_pos, JOINTS[i].max_pos);
        /* 閫熷害璁句负 500 (涓瓑閫熷害)锛岄槻姝㈠姩浣滆繃鐚?*/
        servo_write_pos(JOINTS[i].id, clamped, 500);
    }
}
/* ============ 鏍稿績绾跨▼锛?0Hz 瑙嗚杩借釜 ============ */
static void robot_core_task(void *arg)
{
    vision_target_t target;
    kin_pos_t       target_pos;
    kin_joints_t    target_joints;
    int16_t         servo_cmd[SERVO_NUM_JOINTS];

    /* 鏈熸湜鐨勬湯绔厱閮ㄤ刊浠拌 (寮у害)銆傚鏋滄槸娓呮磥闀滈潰锛屽彲鑳介渶瑕佷繚鎸佸埛瀛愪笌闀滈潰鍨傜洿 */
    float desired_psi = 0.0f;

    while (1) {
        /* 1. 浠庝俊绠遍噷鈥滄媺鈥濆彇鏈€鏂扮殑瑙嗚鐩爣 */
        bool has_target = vision_api_get_latest(&target);

        if (has_target && target.is_valid) {

            /* 銆愰鐣欏彛瀛愩€戣繖閲屼互鍚庡彲浠ュ姞涓婃墜鐪兼爣瀹氱殑鐭╅樀骞崇Щ绠楁硶
             * 姣斿锛?target_pos.x = target.x + 鎽勫儚澶碭杞村亸绉婚噺;
             */
            target_pos.x = target.x;
            target_pos.y = target.y;
            target_pos.z = target.z;

            /* 2. 绌烘皵澧欐鏌ワ細鐩爣鏄惁鍦ㄦ満姊拌噦鐗╃悊鑷傚睍鑼冨洿鍐咃紵 */
            if (kin_check_workspace(&target_pos)) {

                /* 3. 鏍稿績璁＄畻锛氳皟鐢ㄩ€嗚繍鍔ㄥ瑙ｇ畻鍏宠妭瑙掑害 (寮у害) */
                kin_result_t ik_res = kin_inverse(&target_pos, desired_psi, &target_joints);

                if (ik_res == KIN_OK) {
                    /* 4. 鍗曚綅杞崲锛氬姬搴?-> 0-4095 鐨勮埖鏈烘鏁?*/
                    servo_cmd[0] = kin_rad_to_pos(0, target_joints.j[0]);
                    servo_cmd[1] = kin_rad_to_pos(1, target_joints.j[1]);
                    servo_cmd[2] = kin_rad_to_pos(2, target_joints.j[2]);
                    servo_cmd[3] = kin_rad_to_pos(3, target_joints.j[3]);

                    /* 5. 鑵曢儴缈昏浆(J5)鍜屽す鐖?J6) 鏍规嵁涓氬姟闇€姹傚崟鐙帶鍒讹紝杩欓噷鍏堜繚鎸丠ome浣?*/
                    servo_cmd[4] = JOINTS[4].home_pos;
                    servo_cmd[5] = JOINTS[5].home_pos; // 浠ュ悗濡傛灉鏄埛瀛愶紝灏卞湪杩欓噷缁欏姏

                    /* 6. 鍙戦€佹寚浠ょ粰搴曞眰涓插彛椹卞姩 */
                    write_all_positions(servo_cmd);
                } else {
                    ESP_LOGW(TAG, "IK Failed: Target Unreachable or Singular");
                }
        /* 涓ユ牸鎺у埗鍒锋柊鐜囷細20ms (50Hz) */
        vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
}
/* ============ 鍏叡鍒濆鍖栨帴鍙?============ */
void robot_core_init(void)
{
    ESP_LOGI(TAG, "Robot Core Init... Turning on torques.");

    int16_t home_cmd[SERVO_NUM_JOINTS];

    /* 1. 寮€鍚墍鏈夎埖鏈虹殑鎵煩閿?*/
    for (int i = 0; i < SERVO_NUM_JOINTS; i++) {
        home_cmd[i] = JOINTS[i].home_pos;
    }

    /* 2. 璁╂満姊拌噦缂撴參鍥炲埌瀹夊叏鐨勯浂鐐逛綅缃?*/
    ESP_LOGI(TAG, "Moving to HOME position...");
    write_all_positions(home_cmd);

    /* 3. 绛夊緟瀹冭蛋鍒颁綅 */
    vTaskDelay(pdMS_TO_TICKS(1500));
}
/* ============ 鍚姩鏍稿績杩借釜绾跨▼ ============ */
void robot_core_start_task(void)
{
    /* 鍒涘缓 FreeRTOS 浠诲姟
     * 鍙傛暟渚濇涓猴細浠诲姟鍑芥暟銆佷换鍔″悕绉般€佹爤绌洪棿(4096瀛楄妭闃叉诞鐐规孩鍑?銆佷紶閫掑弬鏁般€佷紭鍏堢骇銆佷换鍔″彞鏌?
     */
    xTaskCreate(robot_core_task, "robot_core_task", 4096, NULL, 5, NULL);
}