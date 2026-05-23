# QianRuShi

QianRuShi is an ESP32-P4 based embedded AI project. The repository is organized
around four independent work areas:

- vision model development for mirror stain / water-drop detection
- smart-car firmware, including the original Arduino version and the ESP-IDF
  ESP32-P4 port
- robot-arm / actuator firmware that receives AI vision coordinates and drives
  servos through kinematics logic
- project documents, hardware files, board references, and original archives

## Directory layout

```text
.
|-- docs/
|   |-- competition/        Competition task and requirement documents
|   |-- project/            Project introduction and application references
|   `-- esp32_p4/           ESP32-P4 board and function documentation
|-- hardware/
|   `-- pcb/                Schematic and Gerber package
|-- vision/
|   `-- mirror_stain_detection/
|       |-- 01_training/    FP32 training code
|       |-- 02_onnx_export/ ONNX export code
|       |-- 03_quantization/ INT8 quantization / ESP-DL export code
|       |-- 04_pc_inference/ PC-side inference verification
|       `-- shared/         Shared class names and Python requirements
|-- firmware/
|   |-- smartcar_arduino_esp32/ Original ESP32 Arduino smart-car sketch
|   |-- smartcar_espidf_p4/     Native ESP-IDF port for ESP32-P4
|   `-- robot_arm_espidf/       AI vision robot-arm control firmware
|-- experiments/
|   `-- esp32_idf_hello_world/  ESP-IDF hello_world experiment project
`-- archives/               Original zip packages
```

## Code modules

`vision/mirror_stain_detection` is the AI model pipeline. Keep its training,
ONNX export, quantization, PC inference, and shared files together because they
form one model deployment workflow.

`firmware/smartcar_arduino_esp32` is the original Arduino implementation for
the smart car. It is kept separate from the ESP-IDF port so the original sketch
can still be opened directly in Arduino IDE.

`firmware/smartcar_espidf_p4` is the ESP32-P4 ESP-IDF smart-car project. Build
it from an ESP-IDF shell with the instructions in its own README.

`firmware/robot_arm_espidf` is the actuator / robot-arm firmware. Its main flow
initializes servo communication, receives AI vision data, initializes
kinematics, and starts the tracking task.

`experiments/esp32_idf_hello_world` is only a board bring-up / ESP-IDF learning
example and should not be mixed with production firmware.
