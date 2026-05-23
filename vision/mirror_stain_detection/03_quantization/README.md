# 任务三：INT8 模型量化压缩（核心战役）

## 目标
把 FP32 模型（32位浮点，4字节/参数）暴力压缩为 INT8（8位整数，1字节/参数），
实现 4× 内存压缩 + 2~4× 推理加速，让 ESP32-P4 能装得下、跑得动。

## 量化原理
```
FP32: -3.14159265  →  用 4 字节存储 16,777,216 种可能值
INT8: -3.1         →  用 1 字节存储 256 种可能值 (-128 ~ 127)
                       ↑ 这就是精度损耗的来源
```

## 目录结构
```
03_quantization/
├── calibration_data/    ← 【你需要手动放图片在这里！】
├── output/              ← 量化结果自动生成在这里
│   ├── best_int8.onnx   ← INT8 量化模型
│   ├── model_data.hpp   ← C 字节数组（烧录用）
│   └── model_info.hpp   ← C 配置常量
├── quantize_int8.py     ← 主量化脚本
├── evaluate_quantized.py← 精度对比脚本
└── espdl_export.py      ← 生成 ESP-DL C 头文件
```

## 步骤

### 第一步：准备校准数据
将 **50~100 张** 代表性图片复制到 `calibration_data/` 目录。

校准图片要求：
- 直接复制，无需标注
- 包含各种类型的污渍（水滴/油脂/大面积水痕）
- 包含几张干净镜子的图片（负样本）
- 不同光线条件（日光、室内灯、阴天）
- 从训练集里随机抽取即可

### 第二步：运行量化
```bash
cd 03_quantization
python quantize_int8.py
```
预期耗时：1~5 分钟（纯 CPU）

预期输出：
```
FP32 大小 : 393.2 KB
INT8 大小 : 112.4 KB   ← 约 3.5× 压缩
```

### 第三步：评估精度损耗
```bash
python evaluate_quantized.py
```

评估标准：
| 平均误差 | 评级 | mAP 预期损耗 |
|---------|------|------------|
| < 0.10 | 优秀 ★★★ | < 1% |
| 0.10~0.30 | 良好 ★★ | 1~3% |
| 0.30~0.60 | 一般 ★ | 3~8% |
| > 0.60 | 较差，需改进 | > 8% |

如果精度损耗过大，尝试：
1. 增加校准图片数量（目标 100 张）
2. 让校准集更有代表性（多种光线、角度、污渍类型）

### 第四步：生成 ESP-DL C 头文件
```bash
python espdl_export.py
```

输出 `output/model_data.hpp` 和 `output/model_info.hpp`，
将这两个文件复制到 ESP-IDF 工程的 `main/` 目录下即可使用。

## 关于 ESP-DL 官方量化工具
乐鑫官方提供了更完整的量化工具链，可以生成针对 ESP32-P4 SIMD 优化的模型。
进阶使用请参考：https://github.com/espressif/esp-dl

本项目使用 ONNX Runtime 量化作为替代方案，输出文件可通过上述工具进一步优化。

## 下一步
```bash
cd ../04_pc_inference
python run_inference.py --image path/to/your/photo.jpg
```
