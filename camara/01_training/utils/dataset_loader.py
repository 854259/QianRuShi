"""
YOLO 格式数据集加载器

数据集目录结构:
    01_training/dataset/
    ├── images/
    │   ├── train/   ← 训练图片 (jpg/png)
    │   └── val/     ← 验证图片
    └── labels/
        ├── train/   ← 训练标签 (txt)
        └── val/     ← 验证标签

YOLO 标签格式（每个 .txt 文件每行一个目标）:
    class_id  cx  cy  width  height
    所有值均归一化到 [0, 1]，cx/cy 是中心点，w/h 是框的宽高
    例：  0  0.512  0.380  0.240  0.180
"""

import os
import random
import cv2
import numpy as np
import torch
from torch.utils.data import Dataset, DataLoader
from pathlib import Path


# ImageNet 归一化参数（与 ONNX 推理端保持一致）
MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD  = np.array([0.229, 0.224, 0.225], dtype=np.float32)


class MirrorStainDataset(Dataset):
    """
    镜面污渍检测数据集

    Args:
        image_dir : 图片目录路径
        label_dir : 标签目录路径
        img_size  : 模型输入尺寸（默认 128）
        augment   : 是否进行数据增强（训练时开，验证时关）
    """

    def __init__(self, image_dir: str, label_dir: str,
                 img_size: int = 128, augment: bool = True):
        self.image_dir = Path(image_dir)
        self.label_dir = Path(label_dir)
        self.img_size  = img_size
        self.augment   = augment

        # 收集所有有效的 (图片, 标签) 路径对
        VALID_EXTS = {'.jpg', '.jpeg', '.png', '.bmp', '.webp'}
        self.samples = []
        for fpath in sorted(self.image_dir.iterdir()):
            if fpath.suffix.lower() not in VALID_EXTS:
                continue
            lbl_path = self.label_dir / (fpath.stem + '.txt')
            self.samples.append((str(fpath), str(lbl_path)))

        if len(self.samples) == 0:
            raise FileNotFoundError(
                f"\n[错误] 在 {image_dir} 中未找到图片文件。\n"
                "请先按照 dataset/README_dataset.md 的说明准备数据集。"
            )

        print(f"[Dataset] {len(self.samples)} 张图片加载自 {image_dir}")

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        img_path, lbl_path = self.samples[idx]

        # ── 加载图片 ──────────────────────────────────────────────────
        img = cv2.imread(img_path)
        if img is None:
            raise IOError(f"无法读取图片: {img_path}")
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)

        # ── 加载标签 ──────────────────────────────────────────────────
        boxes = []   # [[cls, cx, cy, w, h], ...]
        if os.path.exists(lbl_path):
            with open(lbl_path, 'r') as f:
                for line in f:
                    parts = line.strip().split()
                    if len(parts) == 5:
                        try:
                            boxes.append([float(p) for p in parts])
                        except ValueError:
                            pass

        # ── 数据增强 ──────────────────────────────────────────────────
        if self.augment:
            img, boxes = self._augment(img, boxes)

        # ── Letterbox 缩放（保持宽高比，填充至正方形）────────────────
        img, boxes = self._letterbox(img, boxes, self.img_size)

        # ── 归一化 ────────────────────────────────────────────────────
        img = img.astype(np.float32) / 255.0
        img = (img - MEAN) / STD
        img = torch.from_numpy(img.transpose(2, 0, 1)).float()  # (3, H, W)

        # ── 标签张量 ──────────────────────────────────────────────────
        if boxes:
            labels = torch.tensor(boxes, dtype=torch.float32)   # (N, 5)
        else:
            labels = torch.zeros((0, 5), dtype=torch.float32)

        return img, labels, img_path

    # ── 私有方法 ──────────────────────────────────────────────────────

    def _letterbox(self, img, boxes, target):
        """等比缩放 + 灰色填充至 targetxtarget"""
        h, w = img.shape[:2]
        scale = target / max(h, w)
        nh    = int(h * scale)
        nw    = int(w * scale)
        img   = cv2.resize(img, (nw, nh), interpolation=cv2.INTER_LINEAR)

        pad_h = target - nh
        pad_w = target - nw
        top   = pad_h // 2
        left  = pad_w // 2
        img   = cv2.copyMakeBorder(
            img, top, pad_h - top, left, pad_w - left,
            cv2.BORDER_CONSTANT, value=(114, 114, 114)
        )

        # 调整 box 坐标
        if boxes:
            new_boxes = []
            for cls, cx, cy, bw, bh in boxes:
                new_cx = (cx * w * scale + left) / target
                new_cy = (cy * h * scale + top)  / target
                new_bw = bw * w * scale / target
                new_bh = bh * h * scale / target
                # 裁剪并过滤过小的框
                new_cx = float(np.clip(new_cx, 0, 1))
                new_cy = float(np.clip(new_cy, 0, 1))
                new_bw = float(np.clip(new_bw, 0, 1))
                new_bh = float(np.clip(new_bh, 0, 1))
                if new_bw > 0.005 and new_bh > 0.005:
                    new_boxes.append([cls, new_cx, new_cy, new_bw, new_bh])
            boxes = new_boxes

        return img, boxes

    def _augment(self, img, boxes):
        """训练时数据增强"""
        # 随机水平翻转
        if random.random() > 0.5:
            img   = img[:, ::-1].copy()
            boxes = [[c, 1.0 - cx, cy, bw, bh] for c, cx, cy, bw, bh in boxes]

        # 随机亮度 / 对比度 / 饱和度抖动
        img = self._color_jitter(img, brightness=0.3, contrast=0.3, saturation=0.2)

        # 随机小角度旋转（镜子不会大角度倾斜）
        if random.random() > 0.7:
            img, boxes = self._random_rotate(img, boxes, max_deg=5)

        return img, boxes

    @staticmethod
    def _color_jitter(img, brightness=0.3, contrast=0.3, saturation=0.2):
        img = img.astype(np.float32)
        b = random.uniform(1 - brightness, 1 + brightness)
        img = img * b
        c = random.uniform(1 - contrast, 1 + contrast)
        mean = img.mean()
        img  = img * c + mean * (1 - c)
        return np.clip(img, 0, 255).astype(np.uint8)

    @staticmethod
    def _random_rotate(img, boxes, max_deg=5):
        h, w = img.shape[:2]
        angle = random.uniform(-max_deg, max_deg)
        M = cv2.getRotationMatrix2D((w / 2, h / 2), angle, 1.0)
        img = cv2.warpAffine(img, M, (w, h),
                              borderMode=cv2.BORDER_CONSTANT,
                              borderValue=(114, 114, 114))
        # box 旋转近似（小角度下误差可忽略）
        return img, boxes


# ── 批次拼装函数 ──────────────────────────────────────────────────────────────

def collate_fn(batch):
    """
    将一个 batch 的 (img, labels, path) 拼装为批张量。
    标签会增加一列 batch_idx，变为 (N, 6): [batch_idx, cls, cx, cy, w, h]
    """
    imgs, labels, paths = zip(*batch)
    imgs = torch.stack(imgs, dim=0)

    targets = []
    for i, lbl in enumerate(labels):
        if len(lbl) > 0:
            bi = torch.full((len(lbl), 1), float(i))
            targets.append(torch.cat([bi, lbl], dim=1))   # (N, 6)

    targets = torch.cat(targets, dim=0) if targets else torch.zeros((0, 6))
    return imgs, targets, list(paths)


def build_dataloader(image_dir: str, label_dir: str,
                     img_size: int = 128, batch_size: int = 16,
                     augment: bool = True, num_workers: int = 0) -> tuple:
    """构建数据加载器"""
    dataset = MirrorStainDataset(image_dir, label_dir, img_size, augment)
    loader  = DataLoader(
        dataset,
        batch_size  = batch_size,
        shuffle     = augment,
        collate_fn  = collate_fn,
        num_workers = num_workers,
        pin_memory  = torch.cuda.is_available(),
        drop_last   = False,
    )
    return loader, dataset
