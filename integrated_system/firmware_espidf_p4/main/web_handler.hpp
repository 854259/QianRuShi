#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "esp_http_server.h"

enum class SystemMode {
    kManual,
    kAvoid,
};

class WebHandler {
public:
    void begin(const char *ssid, const char *password);
    SystemMode mode() const;
    void run_mode_iteration();
    void set_mode(SystemMode mode);
    void add_log(const std::string &message);

private:
    static esp_err_t root_handler(httpd_req_t *request);
    static esp_err_t command_handler(httpd_req_t *request);
    static esp_err_t data_handler(httpd_req_t *request);
    static esp_err_t history_handler(httpd_req_t *request);

    esp_err_t handle_root(httpd_req_t *request);
    esp_err_t handle_command(httpd_req_t *request);
    esp_err_t handle_data(httpd_req_t *request);
    esp_err_t handle_history(httpd_req_t *request);
    void start_soft_ap(const char *ssid, const char *password);
    void start_http_server();
    void update_speed_records(float speed);
    std::string take_log();

    static constexpr size_t kHistorySize = 50;
    static constexpr size_t kValidSpeedSize = 5;

    httpd_handle_t server_ = nullptr;
    std::atomic<SystemMode> mode_{SystemMode::kManual};
    std::array<float, kHistorySize> speed_history_{};
    std::array<float, kValidSpeedSize> valid_speeds_{};
    size_t history_index_ = 0;
    size_t history_count_ = 0;
    size_t valid_speed_index_ = 0;
    uint32_t last_speed_update_ms_ = 0;
    uint32_t last_log_ms_ = 0;
    std::mutex mode_mutex_;
    std::mutex state_mutex_;
    std::string log_buffer_;
};

extern WebHandler WebSys;
