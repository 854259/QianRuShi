"""
任务一主脚本：训练 TinyMirrorDet

用法:
    cd 01_training
    python train.py                          # 使用默认配置
    python train.py --epochs 200             # 训练更多轮
    python train.py --resume outputs/weights/last.pt  # 断点续训

训练完成后，最优权重保存在 outputs/weights/best.pt
"""

import os
import sys
import time
import yaml
import argparse
import torch
import torch.optim as optim
from torch.optim.lr_scheduler import CosineAnnealingLR
from pathlib import Path

# 将 01_training/ 加入模块搜索路径
ROOT = Path(__file__).parent
sys.path.insert(0, str(ROOT))

from models.detector import TinyMirrorDet, decode_predictions
from utils.dataset_loader import build_dataloader
from utils.loss_functions import YOLOLoss
from utils.metrics import compute_map
from utils.visualize import plot_training_curve


def parse_args():
    p = argparse.ArgumentParser(description='TinyMirrorDet 训练脚本')
    p.add_argument('--data',    default='configs/mirror_stain.yaml', help='数据集配置文件')
    p.add_argument('--config',  default='configs/train_config.yaml', help='训练超参数配置')
    p.add_argument('--resume',  default='',                          help='从此 checkpoint 恢复训练')
    p.add_argument('--epochs',  type=int,   default=None,            help='覆盖配置中的 epochs')
    p.add_argument('--batch',   type=int,   default=None,            help='覆盖配置中的 batch_size')
    p.add_argument('--device',  default='auto',                      help='cuda / cpu / auto')
    return p.parse_args()


def main():
    args = parse_args()

    # ── 加载配置 ──────────────────────────────────────────────────────
    with open(args.data,   encoding='utf-8') as f:
        data_cfg  = yaml.safe_load(f)
    with open(args.config, encoding='utf-8') as f:
        train_cfg = yaml.safe_load(f)

    # 命令行参数覆盖配置文件
    if args.epochs is not None:
        train_cfg['epochs'] = args.epochs
    if args.batch is not None:
        train_cfg['batch_size'] = args.batch

    # ── 设备 ──────────────────────────────────────────────────────────
    if args.device == 'auto':
        device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    else:
        device = torch.device(args.device)

    if device.type == 'cuda':
        print(f'[Train] GPU: {torch.cuda.get_device_name(0)}')
    else:
        print('[Train] 使用 CPU 训练（速度较慢，建议数据集不超过 500 张）')

    # ── 数据集 ────────────────────────────────────────────────────────
    img_size    = train_cfg.get('img_size',    128)
    batch_size  = train_cfg.get('batch_size',  16)
    num_workers = train_cfg.get('num_workers', 0)

    data_root = ROOT / data_cfg['path']
    train_split = data_cfg['train']
    val_split   = data_cfg['val']

    train_loader, train_ds = build_dataloader(
        image_dir   = str(data_root / train_split / 'images'),
        label_dir   = str(data_root / train_split / 'labels'),
        img_size    = img_size,
        batch_size  = batch_size,
        augment     = True,
        num_workers = num_workers,
    )
    val_loader, val_ds = build_dataloader(
        image_dir   = str(data_root / val_split / 'images'),
        label_dir   = str(data_root / val_split / 'labels'),
        img_size    = img_size,
        batch_size  = batch_size,
        augment     = False,
        num_workers = num_workers,
    )

    num_classes = data_cfg.get('nc', 1)
    print(f'[Train] 类别: {num_classes}  训练集: {len(train_ds)} 张  验证集: {len(val_ds)} 张')

    # ── 模型 ──────────────────────────────────────────────────────────
    model = TinyMirrorDet(num_classes=num_classes).to(device)
    model.print_summary()

    # ── 损失函数 ──────────────────────────────────────────────────────
    criterion = YOLOLoss(
        anchors     = TinyMirrorDet.ANCHORS,
        num_classes = num_classes,
        input_size  = img_size,
    )

    # ── 优化器 + 调度器 ───────────────────────────────────────────────
    epochs = train_cfg.get('epochs', 100)
    lr     = train_cfg.get('lr', 0.01)

    optimizer = optim.SGD(
        model.parameters(), lr=lr,
        momentum     = train_cfg.get('momentum', 0.937),
        weight_decay = train_cfg.get('weight_decay', 5e-4),
        nesterov     = True,
    )
    scheduler = CosineAnnealingLR(optimizer, T_max=epochs, eta_min=lr * 0.01)

    # ── 断点续训 ──────────────────────────────────────────────────────
    start_epoch = 0
    best_map    = 0.0
    if args.resume and os.path.exists(args.resume):
        ckpt = torch.load(args.resume, map_location=device)
        model.load_state_dict(ckpt['model'])
        optimizer.load_state_dict(ckpt['optimizer'])
        scheduler.load_state_dict(ckpt.get('scheduler', scheduler.state_dict()))
        start_epoch = ckpt['epoch'] + 1
        best_map    = ckpt.get('best_map', 0.0)
        print(f'[Train] 从 epoch {start_epoch} 恢复，best mAP={best_map:.4f}')

    # ── 输出目录 ──────────────────────────────────────────────────────
    out_dir = ROOT / 'outputs' / 'weights'
    out_dir.mkdir(parents=True, exist_ok=True)

    history = {'train_loss': [], 'val_map': []}

    print(f'\n{"="*55}')
    print(f'开始训练  共 {epochs} 轮  批量大小={batch_size}')
    print(f'{"="*55}\n')

    # ── 训练主循环 ────────────────────────────────────────────────────
    for epoch in range(start_epoch, epochs):
        model.train()
        epoch_loss = 0.0
        t0 = time.time()

        for batch_i, (imgs, targets, _) in enumerate(train_loader):
            imgs    = imgs.to(device)
            targets = targets.to(device)

            optimizer.zero_grad()
            pred = model(imgs)
            loss, loss_dict = criterion(pred, targets)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 10.0)
            optimizer.step()

            epoch_loss += loss.item()

            # 每 10 个 batch 打印一次进度
            if (batch_i + 1) % 10 == 0 or batch_i == 0:
                print(f'  [{epoch+1}/{epochs}] step {batch_i+1}/{len(train_loader)} '
                      f'loss={loss.item():.4f} '
                      f'(box={loss_dict["box"]:.3f} '
                      f'obj={loss_dict["obj"]:.3f} '
                      f'cls={loss_dict["cls"]:.3f})')

        scheduler.step()
        avg_loss = epoch_loss / max(len(train_loader), 1)
        history['train_loss'].append(avg_loss)

        # ── 验证 ──────────────────────────────────────────────────────
        mAP = _validate(model, val_loader, device, num_classes,
                        TinyMirrorDet.ANCHORS, img_size)
        history['val_map'].append(mAP)

        elapsed = time.time() - t0
        lr_now  = optimizer.param_groups[0]['lr']
        print(f'\n  Epoch {epoch+1}/{epochs} | '
              f'avg_loss={avg_loss:.4f} | mAP={mAP:.4f} | '
              f'lr={lr_now:.6f} | {elapsed:.1f}s\n')

        # ── 保存 checkpoint ───────────────────────────────────────────
        ckpt = {
            'epoch':     epoch,
            'model':     model.state_dict(),
            'optimizer': optimizer.state_dict(),
            'scheduler': scheduler.state_dict(),
            'best_map':  best_map,
        }
        torch.save(ckpt, out_dir / 'last.pt')

        if mAP > best_map:
            best_map = mAP
            ckpt['best_map'] = best_map
            torch.save(ckpt, out_dir / 'best.pt')
            print(f'  * 新最优 mAP: {best_map:.4f}  → 已保存 best.pt\n')

    # ── 训练结束 ──────────────────────────────────────────────────────
    print(f'\n{"="*55}')
    print(f'训练完成！最优 mAP: {best_map:.4f}')
    print(f'权重文件: {out_dir / "best.pt"}')
    print(f'下一步: cd ../02_onnx_export && python export_onnx.py')
    print(f'{"="*55}')

    plot_training_curve(history, str(ROOT / 'outputs' / 'training_curve.png'))


@torch.no_grad()
def _validate(model, loader, device, num_classes, anchors, input_size):
    """在验证集上计算 mAP@0.5"""
    model.eval()
    all_preds, all_gts = [], []

    for imgs, targets, _ in loader:
        imgs = imgs.to(device)
        raw  = model(imgs)
        dets = decode_predictions(raw, anchors, input_size, conf_thresh=0.01)

        B = imgs.shape[0]
        for b in range(B):
            d = dets[b]
            all_preds.append(d.cpu() if len(d) > 0 else torch.zeros((0, 6)))

        for b in range(B):
            mask = (targets[:, 0] == b)
            all_gts.append(targets[mask, 1:].cpu())

    mAP, _ = compute_map(all_preds, all_gts,
                          iou_thresh=0.5, num_classes=num_classes)
    return mAP


if __name__ == '__main__':
    main()
