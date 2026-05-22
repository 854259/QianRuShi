# smartcar1 ESP-IDF port for ESP32-P4 Function EV Board

This project is a native ESP-IDF port of the original Arduino smart-car sketch.
It is intended to be built as a standalone ESP-IDF project without modifying
the source sketch.

The component manifest accepts ESP-IDF 5.3 or newer and lets the ESP-IDF
component manager resolve the Wi-Fi remote dependencies on the build machine.

## Preserved functions

- SoftAP web control page with manual driving commands
- Manual, five-sensor trace, and ultrasonic obstacle avoidance modes
- Four-motor differential drive and speed sensor feedback
- Servo sweep for obstacle scans
- Fan output control
- Speed graph data, recent valid speed samples, and web logs

## Target and Wi-Fi note

The ESP32-P4 does not provide Wi-Fi by itself. The ESP32-P4 Function EV Board
routes Wi-Fi through its ESP32-C6 companion chip, so this project declares the
`esp_wifi_remote` managed component and enables the board SDIO defaults in
`sdkconfig.defaults.esp32p4`.

Build from an ESP-IDF shell:

```powershell
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

After boot, connect to the AP:

- SSID: `ESP32_SmartCar`
- Password: `12345678`
- Web page: `http://192.168.4.1/`

## ESP32-P4 Function EV Board pin map

The original sketch uses ESP32-WROOM GPIO numbers that are not exposed on the
P4 Function EV Board J1 header. This port moves car signals to J1 pins:

| Function | GPIO |
| --- | --- |
| Left front motor A/B | 7 / 8 |
| Right front motor A/B | 23 / 21 |
| Left rear motor A/B | 22 / 20 |
| Right rear motor A/B | 6 / 5 |
| Speed sensor | 36 |
| Trace sensors 1..5 | 4 / 3 / 2 / 32 / 33 |
| HC-SR04 trigger / echo | 26 / 27 |
| Scan servo PWM | 48 |
| Fan output | 53 |
| External status LED | 46 |

GPIO26 is also used by the optional LCD sub-board on the P4 Function EV Board.
Move `SMARTCAR_SR04_TRIG_GPIO` in `main/car_config.hpp` if that sub-board is
installed. The status LED pin is intended for an external LED and can be set to
`GPIO_NUM_NC` when unused.

## Electrical assumptions

- The motor pins drive an H-bridge input stage, not motors directly.
- `HC-SR04 ECHO` must be level-shifted to 3.3 V before GPIO27.
- The fan output needs a suitable driver/transistor if the fan current exceeds
  a GPIO output rating.
- Trace sensors are treated as active-low, matching the original logic.
