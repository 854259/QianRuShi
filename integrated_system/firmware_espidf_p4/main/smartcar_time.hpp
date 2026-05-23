#pragma once

#include <algorithm>
#include <cstdint>

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

inline uint64_t smartcar_micros()
{
    return static_cast<uint64_t>(esp_timer_get_time());
}

inline uint32_t smartcar_millis()
{
    return static_cast<uint32_t>(smartcar_micros() / 1000ULL);
}

inline void smartcar_delay_ms(uint32_t milliseconds)
{
    vTaskDelay(pdMS_TO_TICKS(std::max<uint32_t>(1, milliseconds)));
}

inline void smartcar_delay_us(uint32_t microseconds)
{
    esp_rom_delay_us(microseconds);
}
