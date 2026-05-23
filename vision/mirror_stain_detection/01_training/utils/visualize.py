"""可视化工具：绘制检测框 + 训练曲线"""

import cv2
import numpy as np
import matplotlib.pyplot as plt
import torch


def draw_boxes(img_bgr: np.ndarray, detections,
               class_names=None, color=(0, 255, 0), thickness=2) -> np.ndarray:
    """
    在图片上绘制检测框（返回新图片，不修改原始）。

    Args:
        img_bgr    : BGR 格式 numpy 数组
        detections : (N, 6) tensor/array [x1,y1,x2,y2,score,cls]，像素坐标
        class_names: 类别名称列表
    """
    img = img_bgr.copy()
    if isinstance(detections, torch.Tensor):
        detections = detections.cpu().numpy()

    for det in detections:
        x1, y1, x2, y2 = int(det[0]), int(det[1]), int(det[2]), int(det[3])
        score  = float(det[4])
        cls_id = int(det[5])

        cv2.rectangle(img, (x1, y1), (x2, y2), color, thickness)

        name  = class_names[cls_id] if class_names and cls_id < len(class_names) else f'cls{cls_id}'
        label = f'{name} {score:.2f}'

        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
        cv2.rectangle(img, (x1, y1 - th - 6), (x1 + tw + 2, y1), color, -1)
        cv2.putText(img, label, (x1 + 1, y1 - 3),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 0), 1, cv2.LINE_AA)

    return img


def plot_training_curve(history: dict, save_path: str = 'outputs/training_curve.png'):
    """绘制并保存训练 loss 和 mAP 曲线。"""
    fig, axes = plt.subplots(1, 2, figsize=(12, 4))
    fig.suptitle('TinyMirrorDet Training', fontsize=13)

    if 'train_loss' in history and history['train_loss']:
        axes[0].plot(history['train_loss'], 'steelblue', linewidth=1.5)
        axes[0].set_title('Training Loss')
        axes[0].set_xlabel('Epoch')
        axes[0].set_ylabel('Loss')
        axes[0].grid(True, alpha=0.3)

    if 'val_map' in history and history['val_map']:
        axes[1].plot(history['val_map'], 'tomato', linewidth=1.5)
        axes[1].set_title('Validation mAP@0.5')
        axes[1].set_xlabel('Epoch')
        axes[1].set_ylabel('mAP')
        axes[1].set_ylim(0, 1.05)
        axes[1].grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(save_path, dpi=120, bbox_inches='tight')
    plt.close()
    print(f'[Visualize] 训练曲线已保存 → {save_path}')
