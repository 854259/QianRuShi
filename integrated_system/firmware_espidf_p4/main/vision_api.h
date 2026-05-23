#ifndef VISION_API_H
#define VISION_API_H

#include <stdint.h>
#include <stdbool.h>

/* AI 璇嗗埆鍒扮殑鐩爣鏁版嵁缁撴瀯 */
typedef struct {
    float x;        /* 鐩爣鍦ㄧ┖闂翠腑鐨?X 鍧愭爣 (mm) */
    float y;        /* 鐩爣鍦ㄧ┖闂翠腑鐨?Y 鍧愭爣 (mm) */
    float z;        /* 鐩爣鍦ㄧ┖闂翠腑鐨?Z 鍧愭爣 (mm) */
    bool is_valid;  /* true=鍙戠幇姹℃崯鐐瑰苟闇€瑕佹竻娲? false=鏃犵洰鏍?寰呮満 */
} vision_target_t;
/* =======================================================
 * 缁欐満姊拌噦鎺у埗绔?(浣犺嚜宸? 璋冪敤鐨勬帴鍙?
 * ======================================================= */

/* 浣犵殑鎺у埗绾跨▼姣忛殧 20ms 璋冪敤涓€娆¤繖涓嚱鏁帮紝瀹夊叏鍦版妸鏈€鏂板潗鏍団€滄媺鈥濆嚭鏉?*/
bool vision_api_get_latest(vision_target_t *out_target);

/* Integrated AI vision producer writes the latest detected target here. */
void vision_api_publish_target(const vision_target_t *target);

/* Integrated AI vision producer clears the target when no stain is visible. */
void vision_api_clear_target(void);

#endif
