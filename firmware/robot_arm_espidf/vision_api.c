#include "vision_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "VISION_API";

/* 全局信箱变量 */
static vision_target_t s_mailbox = {0};

/* 保护信箱的互斥锁 */
static SemaphoreHandle_t s_mutex = NULL;

void vision_api_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        ESP_LOGI(TAG, "Vision API mailbox initialized.");
    }
}
/* 你专用的读取函数 */
bool vision_api_get_latest(vision_target_t *out_target)
{
    if (s_mutex == NULL || out_target == NULL) {
        return false;
    }

    /* 上锁，安全地把数据拷贝到你的局部变量里 */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out_target = s_mailbox;
    xSemaphoreGive(s_mutex);
    
    return out_target->is_valid;
}