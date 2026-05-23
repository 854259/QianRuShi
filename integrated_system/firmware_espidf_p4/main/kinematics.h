#ifndef KINEMATICS_H
#define KINEMATICS_H

#include <stdint.h>

/* DH 鍙傛暟 (姣背) 鈥?鏍规嵁瀹炴祴璋冩暣 */
#define KIN_D1   95.0f   /* 搴曞骇鍒拌偐閮ㄩ珮搴?*/
#define KIN_A2  103.0f   /* 鑲╅儴鍒拌倶閮ㄨ繛鏉?*/
#define KIN_A3  143.0f   /* 鑲橀儴鍒拌厱閮ㄨ繛鏉?*/
#define KIN_D5   185.0f   /* 鑵曢儴鍒版湯绔暱搴?*/

/* 绗涘崱灏斿潗鏍?(mm) */
typedef struct {
    float x, y, z;
} kin_pos_t;

/* 鍏宠妭瑙?(寮у害), J1-J5 (J6澶圭埅涓嶅弬涓庝綅缃繍鍔ㄥ) */
typedef struct {
    float j[5];
} kin_joints_t;

/* IK 姹傝В缁撴灉 */
typedef enum {
    KIN_OK = 0,
    KIN_UNREACHABLE,
    KIN_SINGULAR,
} kin_result_t;

/* 鍒濆鍖栬繍鍔ㄥ妯″潡 */
void kin_init(void);

/* 鑸垫満浣嶇疆 (0-4095) 涓庡姬搴︿簰杞? idx=0..4 瀵瑰簲 J1..J5 */
float kin_pos_to_rad(int idx, int16_t pos);
int16_t kin_rad_to_pos(int idx, float rad);

/* 姝ｈ繍鍔ㄥ: 鍏宠妭瑙?鈫?鏈浣嶇疆 + 鑵曢儴鎬讳刊浠拌 psi */
void kin_forward(const kin_joints_t *joints, kin_pos_t *out_pos, float *out_psi);

/* 閫嗚繍鍔ㄥ: 鐩爣浣嶇疆 + 鑵曢儴淇话瑙?鈫?鍏宠妭瑙?*/
kin_result_t kin_inverse(const kin_pos_t *target, float psi, kin_joints_t *out_joints);

/* 宸ヤ綔绌洪棿杈圭晫妫€鏌?*/
int kin_check_workspace(const kin_pos_t *pos);

#endif
