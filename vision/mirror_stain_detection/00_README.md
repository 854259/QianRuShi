# 镜面污渍检测 · ESP32-P4 部署完整工程

## 项目目标
在 ESP32-P4 开发板上部署轻量化污渍检测模型，实现实时识别镜面污渍/水滴并输出坐标。

## 四大任务流水线

```
[照片] → 01_training → 02_onnx_export → 03_quantization → 04_pc_inference → [ESP32-P4]
          FP32训练      ONNX无损转换      INT8量化压缩        电脑仿真验证
```

| 任务 | 目录 | 精度 | 核心产出 |
|------|------|------|----------|
| 01 训练 | `01_training/` | FP32 基准精度 | `best.pt` |
| 02 导出 | `02_onnx_export/` | FP32 无损 | `best.onnx` |
| 03 量化 | `03_quantization/` | INT8 (±5%) | `best_int8.onnx` + `.hpp` |
| 04 验证 | `04_pc_inference/` | 真实表现 | 可视化结果 |

## 模型参数 (TinyMirrorDet)
- 参数量：~100K（FP32: 393KB，INT8: 98KB）
- 计算量：~30M FLOPs
- 输入：128×128 RGB
- 输出：8×8 检测网格，3个锚框

## 快速开始
```bash
# 1. 安装依赖
pip install -r shared/requirements.txt

# 2. 准备数据集（详见 01_training/dataset/README_dataset.md）

# 3. 训练
cd 01_training && python train.py

# 4. 导出 ONNX
cd 02_onnx_export && python export_onnx.py

# 5. INT8 量化
cd 03_quantization && python quantize_int8.py

# 6. 电脑验证
cd 04_pc_inference && python run_inference.py --image your_test.jpg
```

## 目录说明
```
c/
├── 00_README.md              ← 本文件
├── shared/                   ← 公共配置
│   ├── requirements.txt      ← 所有 pip 依赖
│   └── class_names.txt       ← 类别定义
├── 01_training/              ← 任务一：训练
├── 02_onnx_export/           ← 任务二：ONNX导出
├── 03_quantization/          ← 任务三：INT8量化
└── 04_pc_inference/          ← 任务四：电脑验证
```
