# 任务一：模型训练 (Float32)

## 目标
在电脑上训练 TinyMirrorDet，让它认识镜面污渍和水滴。
训练完成后 `best.pt` 就是精度天花板——后续所有压缩都以此为基准。

## 模型架构（TinyMirrorDet）
```
输入: 128×128×3 (RGB)
  ↓ Stem Conv 3→24 (stride=2)         → 64×64×24
  ↓ DSConv 24→48  (stride=2)          → 32×32×48
  ↓ DSConv 48→48  (stride=1)          → 32×32×48
  ↓ DSConv 48→96  (stride=2)          → 16×16×96
  ↓ DSConv 96→96  (stride=1)          → 16×16×96
  ↓ DSConv 96→192 (stride=2)          → 8×8×192
  ↓ DSConv 192→192(stride=1)          → 8×8×192
  ↓ Neck Conv1×1 192→96               → 8×8×96
  ↓ Head Conv1×1 96→18                → 8×8×18
输出: 8×8 网格，每格 3 个锚框 × 6 个值 (tx,ty,tw,th,conf,cls)
参数: ~100K  |  FP32: ~393KB  |  INT8: ~98KB
```

## 步骤

### 1. 安装环境
```bash
# 方法一（推荐）：双击运行
setup_environment.bat

# 方法二：手动安装（无 GPU）
pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
pip install onnx onnxruntime opencv-python PyYAML tqdm matplotlib
```

### 2. 准备数据集
详见 `dataset/README_dataset.md`

简要流程：
1. 拍 200+ 张镜面污渍照片
2. 用 LabelImg 标注（`pip install labelImg`）
3. 将图片和标签放入对应目录

### 3. 验证模型架构
```bash
python check_model_size.py
```
预期输出：
```
参数量: 100,530
INT8 大小: 98.2 KB  ← 远小于 1MB 限制 ✓
```

### 4. 开始训练
```bash
python train.py
# 默认 100 轮，每轮打印 loss 和 mAP
# 最优权重自动保存到 outputs/weights/best.pt
```

训练期间观察指标：
| 指标 | 正常范围 | 异常处理 |
|------|---------|---------|
| `loss` 持续下降 | 正常 | 如果 loss=nan，降低 lr |
| `obj loss` 最高 | 约占 60% | 正常现象 |
| `mAP` 到 epoch 50 后 > 0.5 | 目标 | 数据不足则增加数据 |

### 5. 查看训练曲线
训练完成后 `outputs/training_curve.png` 会自动生成。

### 6. 评估模型
```bash
python evaluate.py
python evaluate.py --save-vis  # 同时保存可视化结果到 outputs/eval_vis/
```

## 文件说明
| 文件 | 说明 |
|------|------|
| `models/detector.py` | TinyMirrorDet 模型定义 |
| `utils/dataset_loader.py` | YOLO 格式数据集加载 |
| `utils/loss_functions.py` | YOLO 风格检测损失 |
| `utils/metrics.py` | mAP@0.5 计算 |
| `configs/mirror_stain.yaml` | 数据集路径配置 |
| `configs/train_config.yaml` | 训练超参数 |
| `outputs/weights/best.pt` | 最优模型权重（训练后生成）|

## 下一步
```bash
cd ../02_onnx_export
python export_onnx.py
```
