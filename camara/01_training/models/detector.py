"""
TinyMirrorDet — 专为 ESP32-P4 设计的超轻量单阶段目标检测器

架构特点:
  - 纯 DepthwiseSeparable 卷积，对 INT8 SIMD 友好
  - 单尺度检测头，8x8 特征图
  - FP32 参数量 ~100K (393KB)，INT8 后 ~98KB，远低于 1MB 限制

输入:  (B, 3, 128, 128)   RGB 归一化图像
输出:  (B, 18, 8, 8)      原始 logits，3个锚框 x 6个值
                           6个值 = [tx, ty, tw, th, obj_conf, cls_conf]
"""

import math
import torch
import torch.nn as nn


# ── 基础模块 ──────────────────────────────────────────────────────────────────

class ConvBnRelu(nn.Module):
    """标准卷积 + BN + ReLU6"""
    def __init__(self, in_ch: int, out_ch: int, k: int = 3, s: int = 1, p: int = 1):
        super().__init__()
        self.seq = nn.Sequential(
            nn.Conv2d(in_ch, out_ch, k, stride=s, padding=p, bias=False),
            nn.BatchNorm2d(out_ch),
            nn.ReLU6(inplace=True),
        )

    def forward(self, x):
        return self.seq(x)


class DSConv(nn.Module):
    """
    Depthwise Separable Convolution（深度可分离卷积）
    参数量是普通卷积的 1/8 ~ 1/9，ESP32-P4 INT8 运行效率高
    """
    def __init__(self, in_ch: int, out_ch: int, stride: int = 1):
        super().__init__()
        self.dw  = nn.Conv2d(in_ch, in_ch, 3, stride=stride, padding=1,
                              groups=in_ch, bias=False)
        self.bn1 = nn.BatchNorm2d(in_ch)
        self.pw  = nn.Conv2d(in_ch, out_ch, 1, bias=False)
        self.bn2 = nn.BatchNorm2d(out_ch)
        self.act = nn.ReLU6(inplace=True)

    def forward(self, x):
        x = self.act(self.bn1(self.dw(x)))
        x = self.act(self.bn2(self.pw(x)))
        return x


# ── 主模型 ────────────────────────────────────────────────────────────────────

class TinyMirrorDet(nn.Module):
    """
    TinyMirrorDet 完整架构

    Backbone（特征提取，stride=16）:
      Input  128x128x3
      Stem   → 64x64x24
      DSConv → 32x32x48  (s=2)
      DSConv → 32x32x48  (s=1)
      DSConv → 16x16x96  (s=2)
      DSConv → 16x16x96  (s=1)
      DSConv → 8x8x192   (s=2)
      DSConv → 8x8x192   (s=1)

    Neck（特征压缩）:
      Conv1x1 → 8x8x96

    Head（检测输出）:
      Conv1x1 → 8x8x(3x(5+1)) = 8x8x18
    """

    # 锚框大小（在 128x128 原图像素空间中定义）
    #   小锚框  = 水滴级别污渍
    #   中锚框  = 手指划痕
    #   大锚框  = 大面积污迹
    ANCHORS    = [[13, 13], [32, 32], [64, 64]]
    INPUT_SIZE = 128
    STRIDE     = 16   # 128 / 8 = 16

    def __init__(self, num_classes: int = 1, num_anchors: int = 3):
        super().__init__()
        self.num_classes = num_classes
        self.num_anchors = num_anchors

        # ── Backbone ──────────────────────────────────────────────────
        self.backbone = nn.Sequential(
            ConvBnRelu(3,   24,  k=3, s=2, p=1),   # → 64x64x24    [params: ~464]
            DSConv(24,  48,  stride=2),              # → 32x32x48    [params: ~1.5K]
            DSConv(48,  48,  stride=1),              # → 32x32x48    [params: ~2.9K]
            DSConv(48,  96,  stride=2),              # → 16x16x96    [params: ~5.3K]
            DSConv(96,  96,  stride=1),              # → 16x16x96    [params: ~10.5K]
            DSConv(96,  192, stride=2),              # → 8x8x192     [params: ~19.8K]
            DSConv(192, 192, stride=1),              # → 8x8x192     [params: ~39.4K]
        )                                            # backbone total: ~79.9K

        # ── Neck ──────────────────────────────────────────────────────
        self.neck = ConvBnRelu(192, 96, k=1, s=1, p=0)   # → 8x8x96 [params: ~18.6K]

        # ── Head ──────────────────────────────────────────────────────
        # 输出: num_anchors x (4 bbox + 1 obj + num_classes)
        self.head = nn.Conv2d(96, num_anchors * (5 + num_classes), 1)  # [params: ~1.7K]
        # Total: ~100K params

        self._init_weights()

    def _init_weights(self):
        for m in self.modules():
            if isinstance(m, nn.Conv2d):
                nn.init.kaiming_normal_(m.weight, mode='fan_out', nonlinearity='relu')
                if m.bias is not None:
                    nn.init.zeros_(m.bias)
            elif isinstance(m, nn.BatchNorm2d):
                nn.init.ones_(m.weight)
                nn.init.zeros_(m.bias)
        # 将 head 的 bias 初始化为 -log(99)，使初始 objectness≈0.01
        # 防止训练初期 loss 爆炸
        nn.init.constant_(self.head.bias, -math.log(99.0))

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.backbone(x)   # → (B, 192, 8, 8)
        x = self.neck(x)       # → (B, 96, 8, 8)
        x = self.head(x)       # → (B, 18, 8, 8)
        return x

    def count_params(self) -> tuple:
        total     = sum(p.numel() for p in self.parameters())
        trainable = sum(p.numel() for p in self.parameters() if p.requires_grad)
        return total, trainable

    def print_summary(self):
        total, _ = self.count_params()
        dummy = torch.zeros(1, 3, self.INPUT_SIZE, self.INPUT_SIZE)
        out   = self(dummy)
        print("=" * 50)
        print("TinyMirrorDet Architecture Summary")
        print("=" * 50)
        print(f"  Parameters : {total:,}")
        print(f"  FP32 size  : {total * 4 / 1024:.1f} KB  (训练/导出时)")
        print(f"  INT8 size  : {total / 1024:.1f} KB   (量化后，烧录进P4)")
        print(f"  Input      : {list(dummy.shape)}")
        print(f"  Output     : {list(out.shape)}  (18 = 3anchors x 6values)")
        print(f"  Anchors    : {self.ANCHORS}")
        print("=" * 50)



# ── 预测解码 ──────────────────────────────────────────────────────────────────

def decode_predictions(raw: torch.Tensor,
                       anchors=None,
                       input_size: int = 128,
                       stride: int = 16,
                       conf_thresh: float = 0.25,
                       nms_iou: float = 0.45) -> list:
    """
    将模型原始输出解码为 [x1, y1, x2, y2, conf, cls_id] 格式的检测框。

    Args:
        raw        : (B, A*(5+C), H, W) — 模型原始输出（logits）
        anchors    : 锚框列表 [[w,h], ...] 单位像素（原图空间）
        input_size : 输入图像尺寸 128
        stride     : 特征图步长 16
        conf_thresh: 置信度阈值（低于此值的框丢弃）
        nms_iou    : NMS IoU 阈值

    Returns:
        list of tensors, 每张图片一个 (N, 6) tensor
    """
    if anchors is None:
        anchors = TinyMirrorDet.ANCHORS

    device = raw.device
    B, _, H, W = raw.shape
    A = len(anchors)
    C = raw.shape[1] // A - 5

    # reshape → (B, A, H, W, 5+C)
    raw = raw.view(B, A, 5 + C, H, W).permute(0, 1, 3, 4, 2).contiguous()

    # 构建网格坐标
    gy, gx = torch.meshgrid(
        torch.arange(H, device=device),
        torch.arange(W, device=device),
        indexing='ij'
    )
    grid = torch.stack([gx, gy], dim=-1).float()            # (H, W, 2)
    anc  = torch.tensor(anchors, dtype=torch.float32, device=device)  # (A, 2)

    # 解码 xy, wh, conf, cls
    xy  = (torch.sigmoid(raw[..., :2]) + grid[None, None]) * stride
    wh  = anc[None, :, None, None] * torch.exp(raw[..., 2:4].clamp(-4, 4))
    obj = torch.sigmoid(raw[..., 4:5])
    cls = torch.sigmoid(raw[..., 5:]) if C > 0 else torch.ones_like(obj)

    score  = (obj * cls).max(-1, keepdim=True)[0]
    cls_id = (obj * cls).max(-1, keepdim=True)[1].float()

    x1y1 = xy - wh / 2
    x2y2 = xy + wh / 2
    boxes = torch.cat([x1y1, x2y2, score, cls_id], dim=-1)  # (B, A, H, W, 6)
    boxes = boxes.view(B, -1, 6)

    results = []
    for b in range(B):
        det  = boxes[b]
        keep = det[:, 4] > conf_thresh
        det  = det[keep]
        if len(det) > 0:
            det = _nms(det, nms_iou)
        results.append(det)

    return results


def _nms(boxes: torch.Tensor, iou_thresh: float) -> torch.Tensor:
    """贪心 NMS。boxes: (N, 6) [x1,y1,x2,y2,score,cls]"""
    scores = boxes[:, 4]
    order  = scores.argsort(descending=True)
    keep   = []
    while len(order) > 0:
        i = order[0].item()
        keep.append(i)
        if len(order) == 1:
            break
        rest = order[1:]
        ious = _iou(boxes[i:i+1, :4], boxes[rest, :4]).squeeze(0)
        order = rest[ious < iou_thresh]
    return boxes[keep]


def _iou(a: torch.Tensor, b: torch.Tensor) -> torch.Tensor:
    """a: (M,4), b: (N,4) → (M,N) IoU"""
    area_a = (a[:, 2] - a[:, 0]) * (a[:, 3] - a[:, 1])
    area_b = (b[:, 2] - b[:, 0]) * (b[:, 3] - b[:, 1])
    ix1 = torch.max(a[:, None, 0], b[None, :, 0])
    iy1 = torch.max(a[:, None, 1], b[None, :, 1])
    ix2 = torch.min(a[:, None, 2], b[None, :, 2])
    iy2 = torch.min(a[:, None, 3], b[None, :, 3])
    inter = (ix2 - ix1).clamp(0) * (iy2 - iy1).clamp(0)
    return inter / (area_a[:, None] + area_b[None, :] - inter + 1e-7)


# ── 独立运行时打印模型信息 ────────────────────────────────────────────────────

if __name__ == '__main__':
    model = TinyMirrorDet(num_classes=1)
    model.print_summary()
