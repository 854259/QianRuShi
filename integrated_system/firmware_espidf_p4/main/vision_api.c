#include "vision_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "VISION_API";

/* 鍏ㄥ眬淇＄鍙橀噺 */
static vision_target_t s_mailbox = {0};

/* 淇濇姢淇＄鐨勪簰鏂ラ攣 */
static SemaphoreHandle_t s_mutex = NULL;

void vision_api_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        ESP_LOGI(TAG, "Vision API mailbox initialized.");
    }
}
/* 浣犱笓鐢ㄧ殑璇诲彇鍑芥暟 */
bool vision_api_get_latest(vision_target_t *out_target)
{
    if (s_mutex == NULL || out_target == NULL) {
        return false;
    }

    /* 涓婇攣锛屽畨鍏ㄥ湴鎶婃暟鎹嫹璐濆埌浣犵殑灞€閮ㄥ彉閲忛噷 */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out_target = s_mailbox;
    xSemaphoreGive(s_mutex);

    return out_target->is_valid;
}

void vision_api_publish_target(const vision_target_t *target)
{
    if (s_mutex == NULL || target == NULL) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_mailbox = *target;
    xSemaphoreGive(s_mutex);
}

void vision_api_clear_target(void)
{
    if (s_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_mailbox.is_valid = false;
    xSemaphoreGive(s_mutex);
}
