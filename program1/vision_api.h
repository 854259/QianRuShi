#ifndef VISION_API_H
#define VISION_API_H

#include <stdint.h>
#include <stdbool.h>

/* AI 识别到的目标数据结构 */
typedef struct {
    float x;        /* 目标在空间中的 X 坐标 (mm) */
    float y;        /* 目标在空间中的 Y 坐标 (mm) */
    float z;        /* 目标在空间中的 Z 坐标 (mm) */
    bool is_valid;  /* true=发现污损点并需要清洁, false=无目标/待机 */
} vision_target_t;
/* =======================================================
 * 给机械臂控制端 (你自己) 调用的接口
 * ======================================================= */

/* 你的控制线程每隔 20ms 调用一次这个函数，安全地把最新坐标“拉”出来 */
bool vision_api_get_latest(vision_target_t *out_target);

#endif