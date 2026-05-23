#include "kinematics.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "KIN";

/* Home 浣嶇疆 (鑸垫満鍊? 鈥?涓?robot.c 涓?JOINTS[] 涓€鑷?*/
static const int16_t HOME_POS[5] = { 1500, 1500, 1500, 1500, 1500 };

/* 鑸垫満瀹夎姝ｅ弽鍚戣ˉ鍋?(鏋佸叾閲嶈锛佸鏋滆鍙嶄簡锛屾妸瀵瑰簲鐨?1.0f 鏀逛负 -1.0f) */
static const float JOINT_DIR[5] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

/* MG996 鐗╃悊鏋侀檺 (500us ~ 2500us 瀵瑰簲 -90掳 ~ +90掳) */
#define MG996_MIN_PWM 500
#define MG996_MAX_PWM 2500

/* * 鏄犲皠姣斾緥鎺ㄥ锛? * 2000 涓?PWM 鍗曚綅 (2500 - 500) 瀵瑰簲 180搴?(鍗?PI 寮у害)
 */
#define POS_TO_RAD  (M_PI / 2000.0f)
#define RAD_TO_POS  (2000.0f / M_PI)

/* 宸ヤ綔绌洪棿闄愬埗 (mm) */
#define WS_MAX_REACH  (KIN_A2 + KIN_A3 + KIN_D5)  /* ~290mm */
#define WS_MIN_REACH  150.0f
#define WS_Z_MIN     -50.0f
#define WS_Z_MAX      (KIN_D1 + WS_MAX_REACH)

void kin_init(void)
{
    ESP_LOGI(TAG, "Kinematics init: d1=%.0f a2=%.0f a3=%.0f d5=%.0f mm",
             KIN_D1, KIN_A2, KIN_A3, KIN_D5);
    ESP_LOGI(TAG, "Max reach: %.0f mm", WS_MAX_REACH);
}

float kin_pos_to_rad(int idx, int16_t pos)
{
    if (idx < 0 || idx > 4) return 0.0f;
   /* 瑙掑害 = (褰撳墠鑴夊 - 涓綅1500) * 寮у害姣斾緥 * 姝ｅ弽鍚戠郴鏁?*/
    return (float)(pos - HOME_POS[idx]) * POS_TO_RAD * JOINT_DIR[idx];
}

int16_t kin_rad_to_pos(int idx, float rad)
{
    if (idx < 0 || idx > 4) return HOME_POS[0];
    /* 鑴夊 = 涓綅1500 + (寮у害 * 鑴夊姣斾緥 * 姝ｅ弽鍚戠郴鏁? */
    int32_t pos = HOME_POS[idx] + (int32_t)roundf(rad * RAD_TO_POS * JOINT_DIR[idx]);

    /* 閽堝 MG996 鐨勯槻鎾為挸浣嶄繚鎶?*/
    if (pos < MG996_MIN_PWM) pos = MG996_MIN_PWM;
    if (pos > MG996_MAX_PWM) pos = MG996_MAX_PWM;
    return (int16_t)pos;
}

void kin_forward(const kin_joints_t *joints, kin_pos_t *out_pos, float *out_psi)
{
    float t1 = joints->j[0];  /* J1: 搴曞骇鏃嬭浆 */
    float t2 = joints->j[1];  /* J2: 鑲╅儴 */
    float t3 = joints->j[2];  /* J3: 鑲橀儴 */
    float t4 = joints->j[3];  /* J4: 鑵曢儴淇话 */
    /* J5: 鑵曢儴缈昏浆, 涓嶅奖鍝嶄綅缃?*/

    float c1 = cosf(t1), s1 = sinf(t1);

    /* 骞抽潰鍐呯殑鎶曞奖璺濈鍜岄珮搴?*/
    float t23  = t2 + t3;
    float t234 = t23 + t4;

    float r = KIN_A2 * cosf(t2) + KIN_A3 * cosf(t23) + KIN_D5 * cosf(t234);
    float z = KIN_D1 + KIN_A2 * sinf(t2) + KIN_A3 * sinf(t23) + KIN_D5 * sinf(t234);

    out_pos->x = c1 * r;
    out_pos->y = s1 * r;
    out_pos->z = z;

    if (out_psi) {
        *out_psi = t234;  /* 鑵曢儴鎬讳刊浠拌 */
    }
}

kin_result_t kin_inverse(const kin_pos_t *target, float psi, kin_joints_t *out_joints)
{
    float x = target->x;
    float y = target->y;
    float z = target->z;

    /* === J1: 搴曞骇鏃嬭浆 === */
    float r_xy = sqrtf(x * x + y * y);
    float t1;
    if (r_xy < 1.0f) {
        /* 澶潬杩?Z 杞? J1 涓嶇‘瀹?鈫?淇濇寔褰撳墠鍊?*/
        t1 = out_joints->j[0];
    } else {
        t1 = atan2f(y, x);
    }

    /* === 鍘婚櫎 d5 鐨勮础鐚? 姹傝厱蹇?=== */
    float r  = r_xy - KIN_D5 * cosf(psi);
    float wz = z - KIN_D1 - KIN_D5 * sinf(psi);

    /* === J2, J3: 浜岃繛鏉?IK (浣欏鸡瀹氱悊) === */
    float D_sq = r * r + wz * wz;
    float D = sqrtf(D_sq);

    /* 鍙揪鎬ф鏌?*/
    if (D > (KIN_A2 + KIN_A3 - 0.1f)) {
        return KIN_UNREACHABLE;
    }
    if (D < fabsf(KIN_A2 - KIN_A3) + 0.1f) {
        return KIN_UNREACHABLE;
    }

    float cos_t3 = (D_sq - KIN_A2 * KIN_A2 - KIN_A3 * KIN_A3) / (2.0f * KIN_A2 * KIN_A3);

    /* 鏁板€奸挸浣嶉槻姝?acos 瓒婄晫 */
    if (cos_t3 > 1.0f) cos_t3 = 1.0f;
    if (cos_t3 < -1.0f) cos_t3 = -1.0f;

    /* 鍙栬倶涓婅В (elbow-up) */
    float t3 = atan2f(sqrtf(1.0f - cos_t3 * cos_t3), cos_t3);

    float alpha = atan2f(wz, r);
    float beta  = atan2f(KIN_A3 * sinf(t3), KIN_A2 + KIN_A3 * cos_t3);
    float t2 = alpha - beta;

    /* === J4: 鑵曢儴淇话琛ュ伩 === */
    float t4 = psi - t2 - t3;

    /* === 濂囧紓鎬ф鏌?=== */
    if (fabsf(cos_t3) > 0.98f) {
        return KIN_SINGULAR;
    }

    out_joints->j[0] = t1;
    out_joints->j[1] = t2;
    out_joints->j[2] = t3;
    out_joints->j[3] = t4;
    /* j[4] (J5) 涓嶇敱浣嶇疆 IK 鎺у埗 */

    return KIN_OK;
}

int kin_check_workspace(const kin_pos_t *pos)
{
    float r_xy = sqrtf(pos->x * pos->x + pos->y * pos->y);
    float r_total = sqrtf(r_xy * r_xy + (pos->z - KIN_D1) * (pos->z - KIN_D1));

    if (r_total > WS_MAX_REACH * 0.99f) return 0;
    if (r_total < WS_MIN_REACH) return 0;
    if (pos->z < WS_Z_MIN) return 0;
    if (pos->z > WS_Z_MAX) return 0;

    return 1;
}
