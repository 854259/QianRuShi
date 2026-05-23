"""
YOLO 风格检测损失函数

损失 = λ_box x L_box  +  λ_obj x L_obj  +  λ_cls x L_cls

其中:
  L_box  = MSE(预测偏移, 目标偏移) + MSE(预测log尺寸, 目标log尺寸)
  L_obj  = BCE(objectness logits, 正负样本mask)
  L_cls  = BCE(class logits, one-hot 类别标签)

目标分配策略: 对每个 GT 框，选 WH 比率最接近的锚框作为正样本。
"""

import torch
import torch.nn.functional as F


class YOLOLoss:
    """
    Args:
        anchors    : 锚框列表 [[w,h], ...] 单位为原图像素（128x128 空间）
        num_classes: 类别数（本项目 = 1）
        input_size : 输入图像尺寸（128）
    """

    def __init__(self, anchors: list, num_classes: int, input_size: int = 128):
        self.anchors     = anchors
        self.num_classes = num_classes
        self.input_size  = input_size
        self.stride      = input_size // 8   # 16（8 = 特征图尺寸）
        self.grid_size   = input_size // self.stride  # 8

        # 各损失权重
        self.lambda_box = 5.0
        self.lambda_obj = 1.0
        self.lambda_cls = 0.5

    def __call__(self, pred: torch.Tensor, targets: torch.Tensor):
        """
        Args:
            pred   : (B, A*(5+C), H, W)  — 模型原始输出
            targets: (N, 6) — [batch_idx, cls, cx, cy, w, h] 归一化坐标

        Returns:
            total_loss: 标量
            loss_dict : {'box': float, 'obj': float, 'cls': float}
        """
        device = pred.device
        B, _, H, W = pred.shape
        A = len(self.anchors)
        C = self.num_classes

        # ── 重整形状为 (B, A, H, W, 5+C) ──────────────────────────────
        pred = pred.view(B, A, 5 + C, H, W).permute(0, 1, 3, 4, 2).contiguous()

        # ── 构建目标张量 ───────────────────────────────────────────────
        t_obj = torch.zeros(B, A, H, W,     device=device)
        t_xy  = torch.zeros(B, A, H, W, 2,  device=device)
        t_wh  = torch.zeros(B, A, H, W, 2,  device=device)
        t_cls = torch.zeros(B, A, H, W, C,  device=device)
        pos   = torch.zeros(B, A, H, W,     dtype=torch.bool, device=device)

        if targets.numel() > 0:
            anc_t = torch.tensor(self.anchors, dtype=torch.float32, device=device)

            # 将归一化坐标 x input_size → 像素坐标
            tgt = targets.clone()
            tgt[:, 2:6] *= self.input_size

            for t in tgt:
                bi  = int(t[0].item())
                cls = int(t[1].item())
                cx, cy = t[2].item(), t[3].item()
                tw, th = t[4].item(), t[5].item()

                # 选最佳锚框（WH 比例最小失真）
                wh_t  = torch.tensor([tw, th], device=device)
                ratio = wh_t[None] / (anc_t + 1e-7)
                score = torch.max(ratio, 1.0 / (ratio + 1e-7)).max(1)[0]
                ai    = score.argmin().item()

                # 目标所在网格格子
                gx = int(cx / self.stride)
                gy = int(cy / self.stride)
                if not (0 <= gx < W and 0 <= gy < H):
                    continue

                # 偏移目标（相对于网格左上角的小数偏移，范围 0~1）
                t_xy[bi, ai, gy, gx] = torch.tensor(
                    [cx / self.stride - gx, cy / self.stride - gy]
                )
                # 尺寸目标（log 空间，使网络可预测任意尺寸比）
                t_wh[bi, ai, gy, gx] = torch.log(wh_t / anc_t[ai] + 1e-7)
                t_obj[bi, ai, gy, gx] = 1.0
                if cls < C:
                    t_cls[bi, ai, gy, gx, cls] = 1.0
                pos[bi, ai, gy, gx] = True

        # ── Objectness 损失（所有位置都参与）──────────────────────────
        loss_obj = F.binary_cross_entropy_with_logits(
            pred[..., 4], t_obj, reduction='mean'
        )

        # ── Box + Class 损失（只有正样本参与）──────────────────────────
        if pos.sum() == 0:
            loss_box = pred.sum() * 0       # 保留梯度图但值为 0
            loss_cls = pred.sum() * 0
        else:
            # Box 损失
            p_xy = torch.sigmoid(pred[pos, :2])
            p_wh = pred[pos, 2:4]
            loss_box = (
                F.mse_loss(p_xy, t_xy[pos], reduction='mean') +
                F.mse_loss(p_wh, t_wh[pos], reduction='mean')
            )

            # Class 损失（单类时 cls 维度 = 1）
            if C > 0:
                loss_cls = F.binary_cross_entropy_with_logits(
                    pred[pos, 5:], t_cls[pos], reduction='mean'
                )
            else:
                loss_cls = pred.sum() * 0

        total = (self.lambda_box * loss_box +
                 self.lambda_obj * loss_obj +
                 self.lambda_cls * loss_cls)

        return total, {
            'box': loss_box.item(),
            'obj': loss_obj.item(),
            'cls': loss_cls.item(),
        }
