#ifndef ROBOT_CORE_H
#define ROBOT_CORE_H

#include <stdint.h>
#include <stdbool.h>

/* 鍒濆鍖栨満姊拌噦鏍稿績閫昏緫锛?
 * 寮€鍚埖鏈烘壄鐭╋紝骞剁紦鎱㈢Щ鍔ㄥ埌瀹夊叏鐨勫師鐐?(Home) 浣嶇疆
 */
void robot_core_init(void);

/* 鍚姩 50Hz 鐨勮瑙夎拷韪笌杩愬姩瀛﹁В绠椾富绾跨▼ */
void robot_core_start_task(void);

#endif