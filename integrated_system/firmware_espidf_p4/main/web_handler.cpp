#include "web_handler.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "avoid_control.hpp"
#include "car_config.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "smartcar_time.hpp"
#include "speed_drive.hpp"
#include "trace_control.hpp"

namespace {
constexpr char TAG[] = "smartcar_web";

constexpr char HTML_PAGE[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-P4 Smart Car</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#101820;color:#edf3f7;font-family:Arial,sans-serif}
main{max-width:760px;margin:auto;padding:18px}header{display:flex;justify-content:space-between;align-items:center;gap:12px}
h1{font-size:28px;margin:0}section{padding:16px 0;border-top:1px solid #344550}
.status{display:grid;grid-template-columns:1fr auto auto;gap:12px;align-items:center;padding:14px;background:#173244}
.speed{font-size:42px;font-weight:700}.unit{font-size:14px;color:#9ec0d5}.mode{padding:8px;background:#24566d}
button{min-height:46px;border:0;background:#1f8a70;color:#fff;font-size:16px;padding:10px;cursor:pointer}
button.active{background:#f08a24}.grid{display:grid;gap:10px}.modes{grid-template-columns:repeat(3,1fr)}
.pad{width:min(300px,100%);grid-template-columns:repeat(3,1fr);margin:auto}.pad button{height:72px;font-size:24px}
.pad .stop{background:#bb3e3e}canvas{display:block;width:100%;height:190px;background:#f4f7f9}
.tools{grid-template-columns:repeat(3,1fr)}pre{min-height:84px;max-height:160px;overflow:auto;margin:0;background:#081015;padding:10px}
ol{margin:0;padding-left:22px}@media(max-width:540px){.modes,.tools{grid-template-columns:1fr}.status{grid-template-columns:1fr auto}}
</style>
</head>
<body><main>
<header><h1>ESP32-P4 Smart Car</h1></header>
<section class="status"><div><div class="speed"><span id="speed">0.00</span></div><div class="unit">cm/s</div></div><div class="mode" id="mode">Manual</div><div id="stable">Stable</div></section>
<section><canvas id="chart"></canvas></section>
<section class="grid modes"><button data-mode="manual" class="active" onclick="setMode('manual')">Manual</button><button data-mode="avoid" onclick="setMode('avoid')">Avoid</button></section>
<section id="manual" class="grid pad"><i></i><button onpointerdown="move('f')" onpointerup="move('s')" onpointercancel="move('s')">&#9650;</button><i></i><button onpointerdown="move('l')" onpointerup="move('s')" onpointercancel="move('s')">&#9664;</button><button class="stop" onclick="move('s')">Stop</button><button onpointerdown="move('r')" onpointerup="move('s')" onpointercancel="move('s')">&#9654;</button><i></i><button onpointerdown="move('b')" onpointerup="move('s')" onpointercancel="move('s')">&#9660;</button><i></i></section>
<section class="grid tools"><button onclick="copySpeeds()">Copy recent speeds</button><button onclick="downloadCsv()">Export CSV</button><button onclick="move('s')">Stop motors</button></section>
<section><h2>Recent valid speeds</h2><ol id="recent"></ol></section>
<section><h2>System log</h2><pre id="log"></pre></section>
</main><script>
const samples=[], csv=['Time,Speed(cm/s)']; const chart=document.getElementById('chart'), ctx=chart.getContext('2d');
function fit(){chart.width=chart.clientWidth*devicePixelRatio;chart.height=chart.clientHeight*devicePixelRatio;draw()} addEventListener('resize',fit);
function q(path){return fetch(path).then(r=>r.json ? r : r)}
function move(go){fetch('/cmd?go='+go)}
function setMode(mode){fetch('/cmd?mode='+mode);document.querySelectorAll('[data-mode]').forEach(b=>b.classList.toggle('active',b.dataset.mode===mode));document.getElementById('manual').style.display=mode==='manual'?'grid':'none';document.getElementById('mode').textContent=mode[0].toUpperCase()+mode.slice(1)}
function log(text){const box=document.getElementById('log');box.textContent+='['+new Date().toLocaleTimeString()+'] '+text+'\n';box.scrollTop=box.scrollHeight}
function draw(){ctx.clearRect(0,0,chart.width,chart.height);const pad=20*devicePixelRatio,w=chart.width-pad*2,h=chart.height-pad*2,m=Math.max(50,...samples);ctx.strokeStyle='#b8cbd7';for(let i=0;i<5;i++){const y=pad+h*i/4;ctx.beginPath();ctx.moveTo(pad,y);ctx.lineTo(pad+w,y);ctx.stroke()}ctx.strokeStyle='#1f8a70';ctx.lineWidth=2*devicePixelRatio;ctx.beginPath();samples.forEach((v,i)=>{const x=pad+w*i/49,y=pad+h-(v/m*h);i?ctx.lineTo(x,y):ctx.moveTo(x,y)});ctx.stroke()}
function copySpeeds(){navigator.clipboard.writeText([...document.querySelectorAll('#recent li')].map(x=>x.textContent).join('\n')).then(()=>log('Recent speeds copied'))}
function downloadCsv(){const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([csv.join('\n')],{type:'text/csv'}));a.download='smartcar_speed.csv';a.click();URL.revokeObjectURL(a.href);log('CSV exported')}
setInterval(()=>fetch('/data').then(r=>r.json()).then(data=>{document.getElementById('speed').textContent=data.speed.toFixed(2);document.getElementById('stable').textContent=data.stable?'Stable':'Changing';samples.push(data.speed);if(samples.length>50)samples.shift();csv.push(new Date().toLocaleTimeString()+','+data.speed.toFixed(2));document.getElementById('recent').innerHTML=data.validSpeeds.map(v=>'<li>'+v.toFixed(2)+' cm/s</li>').join('');if(data.log)log(data.log);draw()}),500);
fit();
</script></body></html>)HTML";

std::string json_escape(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (unsigned char character : value) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\t':
            escaped += "\\t";
            break;
        case '\r':
            escaped += "\\r";
            break;
        default:
            if (character < 0x20) {
                char encoded[7] = {};
                std::snprintf(encoded, sizeof(encoded), "\\u%04x", character);
                escaped += encoded;
            } else {
                escaped += static_cast<char>(character);
            }
            break;
        }
    }
    return escaped;
}

bool query_value(httpd_req_t *request, const char *key, char *value, size_t value_size)
{
    const size_t query_size = httpd_req_get_url_query_len(request) + 1;
    if (query_size <= 1) {
        return false;
    }

    std::string query(query_size, '\0');
    if (httpd_req_get_url_query_str(request, query.data(), query.size()) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query.c_str(), key, value, value_size) == ESP_OK;
}
}

WebHandler WebSys;

void WebHandler::begin(const char *ssid, const char *password)
{
    start_soft_ap(ssid, password);
    start_http_server();
}

void WebHandler::start_soft_ap(const char *ssid, const char *password)
{
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t event_loop_result = esp_event_loop_create_default();
    if (event_loop_result != ESP_OK && event_loop_result != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(event_loop_result);
    }

    esp_netif_create_default_wifi_ap();
    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    wifi_config_t ap_config = {};
    std::snprintf(reinterpret_cast<char *>(ap_config.ap.ssid), sizeof(ap_config.ap.ssid), "%s", ssid);
    std::snprintf(reinterpret_cast<char *>(ap_config.ap.password), sizeof(ap_config.ap.password), "%s", password);
    ap_config.ap.ssid_len = static_cast<uint8_t>(
        std::strlen(reinterpret_cast<char *>(ap_config.ap.ssid)));
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = std::strlen(password) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP started, SSID=%s", ssid);
}

void WebHandler::start_http_server()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    ESP_ERROR_CHECK(httpd_start(&server_, &config));

    const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = this,
    };
    const httpd_uri_t command_uri = {
        .uri = "/cmd",
        .method = HTTP_GET,
        .handler = command_handler,
        .user_ctx = this,
    };
    const httpd_uri_t data_uri = {
        .uri = "/data",
        .method = HTTP_GET,
        .handler = data_handler,
        .user_ctx = this,
    };
    const httpd_uri_t history_uri = {
        .uri = "/history",
        .method = HTTP_GET,
        .handler = history_handler,
        .user_ctx = this,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &root_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &command_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &data_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &history_uri));
}

esp_err_t WebHandler::root_handler(httpd_req_t *request)
{
    return static_cast<WebHandler *>(request->user_ctx)->handle_root(request);
}

esp_err_t WebHandler::command_handler(httpd_req_t *request)
{
    return static_cast<WebHandler *>(request->user_ctx)->handle_command(request);
}

esp_err_t WebHandler::data_handler(httpd_req_t *request)
{
    return static_cast<WebHandler *>(request->user_ctx)->handle_data(request);
}

esp_err_t WebHandler::history_handler(httpd_req_t *request)
{
    return static_cast<WebHandler *>(request->user_ctx)->handle_history(request);
}

esp_err_t WebHandler::handle_root(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebHandler::handle_command(httpd_req_t *request)
{
    bool handled = false;
    char value[16] = {};

    if (query_value(request, "mode", value, sizeof(value))) {
        if (std::strcmp(value, "manual") == 0) {
            set_mode(SystemMode::kManual);
            handled = true;
        } else if (std::strcmp(value, "avoid") == 0) {
            set_mode(SystemMode::kAvoid);
            handled = true;
        }
    }


    if (query_value(request, "go", value, sizeof(value)) && mode() == SystemMode::kManual) {
        if (std::strcmp(value, "f") == 0) {
            CarDrive.run(255, 255);
            add_log("Forward");
        } else if (std::strcmp(value, "b") == 0) {
            CarDrive.run(-255, -255);
            add_log("Reverse");
        } else if (std::strcmp(value, "l") == 0) {
            CarDrive.run(-220, 220);
            add_log("Turn left");
        } else if (std::strcmp(value, "r") == 0) {
            CarDrive.run(220, -220);
            add_log("Turn right");
        } else if (std::strcmp(value, "s") == 0) {
            CarDrive.stop();
            add_log("Stop");
        }
        handled = true;
    }

    httpd_resp_set_type(request, "text/plain");
    return httpd_resp_sendstr(request, handled ? "OK" : "IGNORED");
}

void WebHandler::update_speed_records(float speed)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    const uint32_t now = smartcar_millis();
    if (now - last_speed_update_ms_ >= 500) {
        speed_history_[history_index_] = speed;
        history_index_ = (history_index_ + 1) % speed_history_.size();
        history_count_ = std::min(history_count_ + 1, speed_history_.size());
        last_speed_update_ms_ = now;
    }

    if (speed > 0.0F) {
        valid_speeds_[valid_speed_index_] = speed;
        valid_speed_index_ = (valid_speed_index_ + 1) % valid_speeds_.size();
    }
}

esp_err_t WebHandler::handle_data(httpd_req_t *request)
{
    const float speed = CarDrive.smoothed_speed_cm_s();
    update_speed_records(speed);

    std::ostringstream response;
    response << "{\"speed\":" << speed
             << ",\"stable\":" << (CarDrive.speed_stable() ? "true" : "false")
             << ",\"validSpeeds\":[";

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (size_t index = 0; index < valid_speeds_.size(); ++index) {
            if (index > 0) {
                response << ',';
            }
            response << valid_speeds_[index];
        }
    }

    response << ']';
    const std::string log = take_log();
    if (!log.empty()) {
        response << ",\"log\":\"" << json_escape(log) << '"';
    }
    response << '}';

    const std::string json = response.str();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json.c_str(), json.size());
}

esp_err_t WebHandler::handle_history(httpd_req_t *request)
{
    std::ostringstream response;
    response << '[';
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const size_t oldest_index =
            (history_index_ + speed_history_.size() - history_count_) % speed_history_.size();
        for (size_t offset = 0; offset < history_count_; ++offset) {
            if (offset > 0) {
                response << ',';
            }
            response << speed_history_[(oldest_index + offset) % speed_history_.size()];
        }
    }
    response << ']';

    const std::string json = response.str();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json.c_str(), json.size());
}

SystemMode WebHandler::mode() const
{
    return mode_.load();
}

void WebHandler::run_mode_iteration()
{
    std::lock_guard<std::mutex> lock(mode_mutex_);
    switch (mode()) {
    case SystemMode::kManual:
        break;
    case SystemMode::kAvoid:
        Avoider.run();
        break;
    }
}

void WebHandler::set_mode(SystemMode new_mode)
{
    std::lock_guard<std::mutex> lock(mode_mutex_);
    if (new_mode == mode()) {
        return;
    }

    CarDrive.stop();
    Avoider.stop();
    mode_.store(new_mode);

    switch (new_mode) {
    case SystemMode::kManual:
        add_log("Manual mode");
        break;
    case SystemMode::kAvoid:
        add_log("Avoid mode");
        break;
    }
}


void WebHandler::add_log(const std::string &message)
{
    const uint32_t now = smartcar_millis();
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (now - last_log_ms_ >= 200 || last_log_ms_ == 0) {
        log_buffer_ = message;
        last_log_ms_ = now;
    }
}

std::string WebHandler::take_log()
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::string log = log_buffer_;
    log_buffer_.clear();
    return log;
}
