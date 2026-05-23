# 数据集准备指南

## 目录结构
```
dataset/
├── train/
│   ├── images/    ← 训练图片（建议 200～500 张）
│   └── labels/    ← 对应 YOLO 格式标签
└── val/
    ├── images/    ← 验证图片（训练集的 20%）
    └── labels/
```

## 第一步：拍摄照片

拍摄建议：
- **场景多样化**：不同光线（正午、日光灯、阴天）
- **角度多样化**：正面、略微倾斜（15°内）
- **污渍类型**：水滴、手指油脂、擦不干净的水痕、灰尘
- **干净背景**：同时拍一些干净的镜子（负样本）
- **分辨率**：手机拍摄即可，不需要高分辨率（训练时会缩小到 128×128）
- **数量参考**：最少 100 张，推荐 300+ 张

## 第二步：标注图片（使用 LabelImg）

### 安装 LabelImg
```bash
pip install labelImg
labelImg
```

### 标注步骤
1. 打开 LabelImg
2. 选择 **YOLO 格式**（右侧切换）
3. 打开图片目录 `dataset/train/images/`
4. 设置标签保存路径为 `dataset/train/labels/`
5. 为每张图片画框并标记类别 `stain`
6. 快捷键：`W` 画框，`D` 下一张，`A` 上一张，`Ctrl+S` 保存

### 标签文件格式
每张图对应一个 `.txt` 文件，每行格式：
```
class_id  cx  cy  width  height
```
- `class_id` = 0（污渍是唯一类别）
- `cx`, `cy` = 框中心点坐标（归一化 0~1）
- `width`, `height` = 框宽高（归一化 0~1）

例：`stain_001.txt`
```
0 0.512 0.380 0.240 0.180
0 0.720 0.550 0.150 0.120
```

## 第三步：数据集分割

建议比例：80% 训练 / 20% 验证

用以下脚本自动分割（在 `01_training/` 目录运行）：
```python
import os, shutil, random
from pathlib import Path

src_img = Path('dataset/images')   # 所有图片放这里
src_lbl = Path('dataset/labels')   # 所有标签放这里

for split in ['train', 'val']:
    for sub in ['images', 'labels']:
        Path(f'dataset/{split}/{sub}').mkdir(parents=True, exist_ok=True)

all_imgs = list(src_img.glob('*.jpg')) + list(src_img.glob('*.png'))
random.shuffle(all_imgs)
n_val = max(1, int(len(all_imgs) * 0.2))
splits = {'val': all_imgs[:n_val], 'train': all_imgs[n_val:]}

for split_name, img_list in splits.items():
    for img_path in img_list:
        lbl_path = src_lbl / (img_path.stem + '.txt')
        shutil.copy(img_path, f'dataset/{split_name}/images/')
        if lbl_path.exists():
            shutil.copy(lbl_path, f'dataset/{split_name}/labels/')

print(f"训练集: {len(splits['train'])} 张")
print(f"验证集: {len(splits['val'])} 张")
```

## 验证数据集是否正确

运行以下命令检查：
```bash
cd 01_training
python check_model_size.py   # 先确认模型架构正确
python train.py              # 开始训练
```

## 常见错误

| 错误信息 | 原因 | 解决方法 |
|---------|------|---------|
| `No images found` | 图片路径不对 | 确认图片在 `images/train/` 下 |
| `labels 不存在` | 标签文件缺失 | 用 LabelImg 标注并保存 |
| `loss = nan` | 学习率过高 | 将 `train_config.yaml` 中 lr 改为 0.001 |
| mAP 接近 0 | 数据集太小 | 至少准备 100 张已标注图片 |
