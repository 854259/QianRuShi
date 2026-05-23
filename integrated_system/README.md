# QianRuShi Integrated System

This folder integrates the three existing blocks without changing the original
source directories:

- smart-car patrol: copied from `firmware/smartcar_espidf_p4`
- AI vision interface: copied from `firmware/robot_arm_espidf/vision_api.*`
- robot arm: copied from `firmware/robot_arm_espidf`

The ESP-IDF entry point is:

```text
firmware_espidf_p4/main/integrated_app_main.cpp
```

It initializes NVS once, starts the smart-car patrol stack, starts the AI vision
mailbox, initializes kinematics and servos, then starts the robot-arm tracking
task. The runtime loop keeps the smart-car drive and web-mode logic alive while
the robot arm consumes the latest valid AI target from `vision_api_get_latest`.

## Build

Open an ESP-IDF shell and run:

```powershell
cd integrated_system/firmware_espidf_p4
idf.py set-target esp32p4
idf.py build
```

## Vision Reference

`vision_pc_reference` keeps the existing PC-side inference scripts so the model
output format remains visible beside the integrated firmware project. Firmware
code should publish detected targets into the copied `vision_api` mailbox before
the robot-arm task reads them.
