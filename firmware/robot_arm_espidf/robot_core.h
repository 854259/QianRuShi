#ifndef ROBOT_CORE_H
#define ROBOT_CORE_H

#include <stdint.h>
#include <stdbool.h>

/* 初始化机械臂核心逻辑：
 * 开启舵机扭矩，并缓慢移动到安全的原点 (Home) 位置
 */
void robot_core_init(void);

/* 启动 50Hz 的视觉追踪与运动学解算主线程 */
void robot_core_start_task(void);

#endif