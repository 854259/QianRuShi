#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>

#define SERVO_NUM_JOINTS 6

/* 1. 鍒濆鍖?ESP32 鐨?LEDC 纭欢 PWM 妯″潡 */
void servo_init(void);

/* 2. 鍐欏叆鐩爣鑴夊 (鍗曚綅锛氬井绉?us锛岃寖鍥?500-2500)
 * 娉細鏅€氱殑 MG996 鏄€滃叏閫熺柉鐙椻€濓紝杞欢鍙戜粈涔堝畠灏变互鏈€澶х墿鐞嗛€熷害鍐茶繃鍘伙紝
 * 鎵€浠ヤ负浜嗗吋瀹逛笂灞傜殑鍑芥暟绛惧悕锛屾垜浠繚鐣?speed 鍙傛暟锛屼絾鍦ㄥ簳灞傜洿鎺ュ拷鐣ュ畠銆?
 */
void servo_write_pos(uint8_t id, int16_t pulse_width_us, uint16_t speed);

#endif