"""
mAP@0.5 计算（11点插值法）

用法:
    all_preds = [(N,6) tensor per image]  [x1,y1,x2,y2,score,cls]
    all_gts   = [(M,5) tensor per image]  [cls,cx,cy,w,h] 归一化
    mAP, per_class = compute_map(all_preds, all_gts, iou_thresh=0.5)
"""

import numpy as np
import torch


def compute_map(predictions: list, ground_truths: list,
                iou_thresh: float = 0.5, num_classes: int = 1) -> tuple:
    """
    计算 mAP@iou_thresh。

    Args:
        predictions  : 每张图一个 (N, 6) tensor [x1,y1,x2,y2,score,cls]
                       坐标为 model input space (128x128)
        ground_truths: 每张图一个 (M, 5) tensor [cls, cx, cy, w, h] 归一化
        iou_thresh   : IoU 判断 TP 的阈值
        num_classes  : 类别数

    Returns:
        mAP          : 所有类别的平均 AP
        ap_per_class : {cls_id: AP} 字典
    """
    ap_per_class = {}

    for cls_id in range(num_classes):
        tp_list, fp_list, score_list = [], [], []
        n_gt = 0

        for pred, gt in zip(predictions, ground_truths):
            # 按类别过滤
            pred_cls = pred[pred[:, 5] == cls_id] if len(pred) > 0 else pred.new_zeros((0, 6))
            gt_cls   = gt[gt[:, 0] == cls_id]     if len(gt)   > 0 else gt.new_zeros((0, 5))

            n_gt    += len(gt_cls)
            matched  = torch.zeros(len(gt_cls), dtype=torch.bool)

            # 按置信度从高到低排序
            if len(pred_cls) > 0:
                order    = pred_cls[:, 4].argsort(descending=True)
                pred_cls = pred_cls[order]

            for p in pred_cls:
                score_list.append(p[4].item())

                if len(gt_cls) == 0:
                    tp_list.append(0)
                    fp_list.append(1)
                    continue

                # 将 GT 从 [cls,cx,cy,w,h] 归一化 → [x1,y1,x2,y2] 归一化
                gt_xyxy = _cxcywh_to_xyxy(gt_cls[:, 1:])

                # 检测框也需要归一化（除以 input_size=128）
                p_xyxy = p[:4] / 128.0

                ious      = _box_iou(p_xyxy.unsqueeze(0), gt_xyxy)[0]
                best_iou, best_j = ious.max(0)

                if best_iou >= iou_thresh and not matched[best_j]:
                    tp_list.append(1)
                    fp_list.append(0)
                    matched[best_j] = True
                else:
                    tp_list.append(0)
                    fp_list.append(1)

        # 计算 AP
        if len(score_list) == 0 or n_gt == 0:
            ap_per_class[cls_id] = 0.0
            continue

        order = np.argsort(score_list)[::-1]
        tp    = np.array(tp_list)[order]
        fp    = np.array(fp_list)[order]

        tp_cum = np.cumsum(tp)
        fp_cum = np.cumsum(fp)

        recall    = tp_cum / (n_gt + 1e-7)
        precision = tp_cum / (tp_cum + fp_cum + 1e-7)

        ap_per_class[cls_id] = _compute_ap_11pt(recall, precision)

    mAP = float(np.mean(list(ap_per_class.values()))) if ap_per_class else 0.0
    return mAP, ap_per_class


def _compute_ap_11pt(recall: np.ndarray, precision: np.ndarray) -> float:
    """11 点插值法计算 AP"""
    ap = 0.0
    for thr in np.linspace(0, 1, 11):
        p_at_r = precision[recall >= thr].max() if np.any(recall >= thr) else 0.0
        ap += p_at_r / 11.0
    return ap


def _cxcywh_to_xyxy(boxes: torch.Tensor) -> torch.Tensor:
    """(N, 4) [cx,cy,w,h] → [x1,y1,x2,y2]"""
    x1 = boxes[:, 0] - boxes[:, 2] / 2
    y1 = boxes[:, 1] - boxes[:, 3] / 2
    x2 = boxes[:, 0] + boxes[:, 2] / 2
    y2 = boxes[:, 1] + boxes[:, 3] / 2
    return torch.stack([x1, y1, x2, y2], dim=1)


def _box_iou(a: torch.Tensor, b: torch.Tensor) -> torch.Tensor:
    """a: (M,4), b: (N,4), 归一化坐标 → (M,N) IoU"""
    area_a = (a[:, 2] - a[:, 0]) * (a[:, 3] - a[:, 1])
    area_b = (b[:, 2] - b[:, 0]) * (b[:, 3] - b[:, 1])
    ix1 = torch.max(a[:, None, 0], b[None, :, 0])
    iy1 = torch.max(a[:, None, 1], b[None, :, 1])
    ix2 = torch.min(a[:, None, 2], b[None, :, 2])
    iy2 = torch.min(a[:, None, 3], b[None, :, 3])
    inter = (ix2 - ix1).clamp(0) * (iy2 - iy1).clamp(0)
    return inter / (area_a[:, None] + area_b[None, :] - inter + 1e-7)
