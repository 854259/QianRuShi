# 任务二：ONNX 格式导出（无损转换）

## 目标
把 PyTorch 的专有格式（`.pt`）转成 AI 界通用的 ONNX 格式，
不损失任何精度，为后续量化和芯片部署做准备。

## 为什么需要 ONNX？
```
PyTorch .pt  →  只有 PyTorch 能读
ONNX .onnx   →  ESP-DL、TensorRT、OpenVINO、ONNXRuntime 都能读
```
ESP-DL（乐鑫官方 AI 框架）只接受 ONNX 作为输入，所以这一步是必须的。

## 步骤

### 1. 导出 ONNX
```bash
cd 02_onnx_export
python export_onnx.py
```
输出文件：`output/best.onnx`

### 2. 验证导出无损性
```bash
python verify_onnx.py

# 用真实图片验证（更可靠）
python verify_onnx.py --image ../01_training/dataset/val/images/your_photo.jpg
```

预期输出：
```
最大绝对误差: 0.00000345   ← 远小于 1e-4，导出无损 ✓
[PASS] ONNX 与 PyTorch 输出几乎完全一致
```

## ONNX 模型规格
```
输入:  images   float32   [1, 3, 128, 128]
输出:  output   float32   [1, 18, 8, 8]
opset: 12
```

输出解读：
- `[1, 18, 8, 8]` = batch × (3锚框 × 6值) × 8网格高 × 8网格宽
- 6值 = [tx, ty, tw, th, objectness_logit, class_logit]
- 需要 sigmoid + anchor 解码才能得到像素坐标（`run_inference.py` 已实现）

## 生成文件
| 文件 | 说明 |
|------|------|
| `output/best.onnx` | FP32 ONNX 模型（训练后运行才生成）|

## 下一步
```bash
cd ../03_quantization
# 先将 50~100 张照片复制到 calibration_data/ 目录
python quantize_int8.py
```
