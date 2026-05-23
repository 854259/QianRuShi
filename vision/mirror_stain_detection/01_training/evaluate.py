"""
评估脚本：在验证集上计算 mAP 并可视化检测结果

用法:
    cd 01_training
    python evaluate.py --checkpoint outputs/weights/best.pt
    python evaluate.py --checkpoint outputs/weights/best.pt --save-vis  # 保存可视化图片
"""

import sys
import argparse
import torch
import cv2
import numpy as np
from pathlib import Path
from tqdm import tqdm
import yaml

ROOT = Path(__file__).parent
sys.path.insert(0, str(ROOT))

from models.detector import TinyMirrorDet, decode_predictions
from utils.dataset_loader import build_dataloader
from utils.metrics import compute_map
from utils.visualize import draw_boxes

MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD  = np.array([0.229, 0.224, 0.225], dtype=np.float32)


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--checkpoint', default='outputs/weights/best.pt')
    p.add_argument('--data',       default='configs/mirror_stain.yaml')
    p.add_argument('--conf',       type=float, default=0.25, help='置信度阈值')
    p.add_argument('--iou',        type=float, default=0.45, help='NMS IoU 阈值')
    p.add_argument('--save-vis',   action='store_true', help='保存可视化结果图')
    return p.parse_args()


def main():
    args = parse_args()

    with open(args.data, encoding='utf-8') as f:
        data_cfg = yaml.safe_load(f)

    device      = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    num_classes = data_cfg.get('nc', 1)
    class_names = data_cfg.get('names', ['stain'])
    img_size    = 128

    # ── 加载模型 ──────────────────────────────────────────────────────
    model = TinyMirrorDet(num_classes=num_classes).to(device)
    ckpt  = torch.load(args.checkpoint, map_location=device)
    model.load_state_dict(ckpt.get('model', ckpt))
    model.eval()

    epoch    = ckpt.get('epoch', '?')
    best_map = ckpt.get('best_map', 0.0)
    print(f'[Eval] 加载 checkpoint: epoch={epoch}, best_map={best_map:.4f}')

    # ── 数据集 ────────────────────────────────────────────────────────
    data_root = ROOT / data_cfg['path']
    val_split = data_cfg['val']
    val_loader, val_ds = build_dataloader(
        image_dir = str(data_root / val_split / 'images'),
        label_dir = str(data_root / val_split / 'labels'),
        img_size  = img_size,
        augment   = False,
        num_workers = 0,
    )

    vis_dir = ROOT / 'outputs' / 'eval_vis'
    if args.save_vis:
        vis_dir.mkdir(parents=True, exist_ok=True)

    all_preds, all_gts = [], []

    with torch.no_grad():
        for imgs, targets, paths in tqdm(val_loader, desc='Evaluating'):
            imgs = imgs.to(device)
            raw  = model(imgs)
            dets = decode_predictions(raw, TinyMirrorDet.ANCHORS, img_size,
                                       conf_thresh=args.conf, nms_iou=args.iou)

            B = imgs.shape[0]
            for b in range(B):
                d = dets[b]
                all_preds.append(d.cpu() if len(d) > 0 else torch.zeros((0, 6)))

                mask = (targets[:, 0] == b)
                all_gts.append(targets[mask, 1:].cpu())

                # 可视化
                if args.save_vis:
                    _save_vis(imgs[b], d.cpu(), paths[b], vis_dir,
                              class_names, img_size)

    mAP, per_class = compute_map(all_preds, all_gts,
                                  iou_thresh=0.5, num_classes=num_classes)

    print(f'\n{"="*45}')
    print(f'验证集评估结果')
    print(f'{"="*45}')
    print(f'  mAP@0.5: {mAP:.4f}  ({mAP*100:.1f}%)')
    for cls_id, ap in per_class.items():
        name = class_names[cls_id] if cls_id < len(class_names) else f'cls{cls_id}'
        print(f'  AP[{name}]: {ap:.4f}')
    print(f'{"="*45}')

    if mAP < 0.5:
        print('\n[建议] mAP 低于 50%，请检查:')
        print('  1. 数据集标签是否正确（用 LabelImg 验证）')
        print('  2. 训练是否充分（尝试增加 epochs）')
        print('  3. 数据量是否足够（建议至少 200 张）')


def _save_vis(img_tensor, detections, img_path, vis_dir, class_names, img_size):
    """将检测结果绘制到图片并保存"""
    # 反归一化
    img = img_tensor.permute(1, 2, 0).numpy()
    img = (img * [0.229, 0.224, 0.225] + [0.485, 0.456, 0.406]) * 255
    img = np.clip(img, 0, 255).astype(np.uint8)
    img_bgr = cv2.cvtColor(img, cv2.COLOR_RGB2BGR)

    if len(detections) > 0:
        img_bgr = draw_boxes(img_bgr, detections, class_names)

    save_path = vis_dir / (Path(img_path).stem + '_eval.jpg')
    cv2.imwrite(str(save_path), img_bgr)


if __name__ == '__main__':
    main()
