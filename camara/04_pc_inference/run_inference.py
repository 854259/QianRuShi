"""
任务四：电脑端仿真推理验证

加载 INT8 量化后的 ONNX 模型，对测试图片运行推理，
在屏幕上画出检测框并打印 (X, Y) 中心坐标。

这里的输出即是烧录进 ESP32-P4 后的真实表现——所见即所得。

用法:
    cd 04_pc_inference
    python run_inference.py --image test_images/your_photo.jpg
    python run_inference.py --image test_images/your_photo.jpg --conf 0.3 --save
    python run_inference.py --fp32  # 使用 FP32 ONNX（对比基准）
"""

import sys
import os
import argparse
import numpy as np
import cv2
import onnxruntime as ort
from pathlib import Path

# ── 模型常量（与训练时保持完全一致）────────────────────────────────────────────
ANCHORS    = [[13, 13], [32, 32], [64, 64]]
INPUT_SIZE = 128
STRIDE     = 16
MEAN       = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD        = np.array([0.229, 0.224, 0.225], dtype=np.float32)


# ── 预处理 ────────────────────────────────────────────────────────────────────

def letterbox(img: np.ndarray, target: int = 128, pad_val: int = 114):
    """等比缩放 + 灰色填充至 targetxtarget，返回缩放比和填充量"""
    h, w   = img.shape[:2]
    scale  = target / max(h, w)
    nh, nw = int(h * scale), int(w * scale)
    img    = cv2.resize(img, (nw, nh), interpolation=cv2.INTER_LINEAR)

    top    = (target - nh) // 2
    bottom = target - nh - top
    left   = (target - nw) // 2
    right  = target - nw - left
    img    = cv2.copyMakeBorder(img, top, bottom, left, right,
                                 cv2.BORDER_CONSTANT, value=(pad_val,) * 3)
    return img, scale, (left, top)


def preprocess(image_path: str):
    """
    加载图片 → (1, 3, 128, 128) float32 numpy
    返回: (img_tensor, img_orig_bgr, scale, pad)
    """
    img = cv2.imread(image_path)
    if img is None:
        raise FileNotFoundError(f'无法读取图片: {image_path}')

    img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    img_lb, scale, pad = letterbox(img_rgb, INPUT_SIZE)

    img_norm = img_lb.astype(np.float32) / 255.0
    img_norm = (img_norm - MEAN) / STD
    img_tensor = img_norm.transpose(2, 0, 1)[np.newaxis]   # (1, 3, H, W)

    return img_tensor, img, scale, pad


# ── 后处理 ────────────────────────────────────────────────────────────────────

def sigmoid(x: np.ndarray) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-np.clip(x, -20, 20)))


def decode_output(raw: np.ndarray,
                   conf_thresh: float = 0.4,
                   nms_iou: float = 0.45) -> np.ndarray:
    """
    将模型原始输出解码为检测框列表。

    Args:
        raw: (1, 18, 8, 8) — ONNX 模型输出（logits）
    Returns:
        (N, 6) array: [x1, y1, x2, y2, score, cls_id]  单位：128x128 像素空间
    """
    _, _, H, W = raw.shape
    A = len(ANCHORS)
    C = raw.shape[1] // A - 5

    # reshape → (A, H, W, 5+C)
    raw = raw[0].reshape(A, 5 + C, H, W).transpose(0, 2, 3, 1)  # (A, H, W, 5+C)

    # 构建网格
    gy, gx = np.meshgrid(np.arange(H), np.arange(W), indexing='ij')
    grid   = np.stack([gx, gy], axis=-1).astype(np.float32)      # (H, W, 2)
    anc    = np.array(ANCHORS, dtype=np.float32)                  # (A, 2)

    # 解码
    xy   = (sigmoid(raw[..., :2]) + grid[None]) * STRIDE          # (A, H, W, 2)
    wh   = anc[:, None, None] * np.exp(np.clip(raw[..., 2:4], -4, 4))
    conf = sigmoid(raw[..., 4:5])
    cls  = sigmoid(raw[..., 5:]) if C > 0 else np.ones_like(conf)

    score  = (conf * cls).max(axis=-1, keepdims=True)
    cls_id = (conf * cls).argmax(axis=-1, keepdims=True).astype(np.float32)

    x1y1 = xy - wh / 2
    x2y2 = xy + wh / 2
    boxes = np.concatenate([x1y1, x2y2, score, cls_id], axis=-1)  # (A, H, W, 6)
    boxes = boxes.reshape(-1, 6)                                    # (A*H*W, 6)

    # 按置信度过滤
    keep  = boxes[:, 4] > conf_thresh
    boxes = boxes[keep]

    if len(boxes) == 0:
        return np.zeros((0, 6), dtype=np.float32)

    return _nms(boxes, nms_iou)


def _nms(boxes: np.ndarray, iou_thresh: float) -> np.ndarray:
    """贪心 NMS"""
    order = boxes[:, 4].argsort()[::-1]
    keep  = []
    while len(order) > 0:
        i = order[0]
        keep.append(i)
        if len(order) == 1:
            break
        ious  = _iou(boxes[i:i+1, :4], boxes[order[1:], :4])[0]
        order = order[1:][ious < iou_thresh]
    return boxes[keep]


def _iou(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    ix1 = np.maximum(a[:, 0], b[:, 0])
    iy1 = np.maximum(a[:, 1], b[:, 1])
    ix2 = np.minimum(a[:, 2], b[:, 2])
    iy2 = np.minimum(a[:, 3], b[:, 3])
    inter = np.maximum(0, ix2 - ix1) * np.maximum(0, iy2 - iy1)
    aa = (a[:, 2] - a[:, 0]) * (a[:, 3] - a[:, 1])
    ab = (b[:, 2] - b[:, 0]) * (b[:, 3] - b[:, 1])
    return inter / (aa + ab - inter + 1e-7)


# ── 结果绘制 ──────────────────────────────────────────────────────────────────

def draw_and_print(img_bgr: np.ndarray, detections: np.ndarray,
                    scale: float, pad: tuple,
                    class_names=None) -> np.ndarray:
    """
    将 128x128 空间中的检测框映射回原始图像，绘制并打印坐标。
    """
    img = img_bgr.copy()
    h0, w0 = img.shape[:2]
    pad_l, pad_t = pad

    if len(detections) == 0:
        print('  (未检测到目标，尝试降低 --conf 阈值)')
        return img

    for det in detections:
        x1, y1, x2, y2, score, cls_id = det
        cls_id = int(cls_id)
        name   = class_names[cls_id] if class_names and cls_id < len(class_names) else f'cls{cls_id}'

        # 映射回原图坐标
        x1_o = (x1 - pad_l) / scale
        y1_o = (y1 - pad_t) / scale
        x2_o = (x2 - pad_l) / scale
        y2_o = (y2 - pad_t) / scale

        cx = (x1_o + x2_o) / 2
        cy = (y1_o + y2_o) / 2

        # 控制台输出（ESP32-P4 烧录后的输出格式一致）
        print(f'  [{name}]  置信度={score:.3f}  '
              f'中心坐标=({cx:.1f}, {cy:.1f})  '
              f'框=[{x1_o:.0f},{y1_o:.0f},{x2_o:.0f},{y2_o:.0f}]')

        # 裁剪到图像范围
        x1_o = max(0, int(x1_o))
        y1_o = max(0, int(y1_o))
        x2_o = min(w0 - 1, int(x2_o))
        y2_o = min(h0 - 1, int(y2_o))

        # 绘制检测框
        cv2.rectangle(img, (x1_o, y1_o), (x2_o, y2_o), (0, 255, 0), 2)

        # 绘制标签背景
        label = f'{name} {score:.2f}'
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.6, 1)
        cv2.rectangle(img, (x1_o, y1_o - th - 8), (x1_o + tw + 4, y1_o), (0, 200, 0), -1)
        cv2.putText(img, label, (x1_o + 2, y1_o - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 0), 1, cv2.LINE_AA)

        # 绘制中心十字（红色）
        cx_i, cy_i = int(cx), int(cy)
        cv2.drawMarker(img, (cx_i, cy_i), (0, 0, 255),
                       cv2.MARKER_CROSS, 20, 2, cv2.LINE_AA)

        # 坐标标注
        coord_label = f'({cx:.0f},{cy:.0f})'
        cv2.putText(img, coord_label, (cx_i + 5, cy_i - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 0, 255), 1, cv2.LINE_AA)

    return img


# ── 主函数 ────────────────────────────────────────────────────────────────────

def run_inference(model_path: str, image_path: str,
                   conf_thresh: float = 0.4, nms_iou: float = 0.45,
                   class_names=None, save: bool = False, show: bool = True):
    """在单张图片上运行推理并显示结果"""

    # 加载 ONNX 模型
    sess       = ort.InferenceSession(model_path, providers=['CPUExecutionProvider'])
    input_name = sess.get_inputs()[0].name

    model_size = Path(model_path).stat().st_size / 1024
    print(f'[Inference] 模型: {model_path}  ({model_size:.1f} KB)')
    print(f'[Inference] 图片: {image_path}')
    print(f'[Inference] 置信度阈值: {conf_thresh}  NMS IoU: {nms_iou}')
    print()

    # 预处理
    img_tensor, img_orig, scale, pad = preprocess(image_path)

    # 推理
    raw = sess.run(None, {input_name: img_tensor})[0]

    # 解码
    dets = decode_output(raw, conf_thresh, nms_iou)

    print(f'[结果] 检测到 {len(dets)} 个目标:')
    img_out = draw_and_print(img_orig, dets, scale, pad, class_names)

    # 保存结果图片
    if save:
        result_dir = Path(__file__).parent / 'test_images' / 'results'
        result_dir.mkdir(exist_ok=True)
        save_path = result_dir / (Path(image_path).stem + '_result.jpg')
        cv2.imwrite(str(save_path), img_out)
        print(f'\n[保存] → {save_path}')

    # 显示结果
    if show:
        win_title = 'Mirror Stain Detection (按任意键关闭)'
        cv2.imshow(win_title, img_out)
        print('\n[提示] 按任意键关闭窗口...')
        cv2.waitKey(0)
        cv2.destroyAllWindows()

    return dets


def parse_args():
    p = argparse.ArgumentParser(description='镜面污渍检测 - 电脑端推理验证')
    p.add_argument('--model',   default='../03_quantization/output/best_int8.onnx',
                   help='INT8 ONNX 模型路径')
    p.add_argument('--fp32',    action='store_true',
                   help='使用 FP32 ONNX（基准对比，默认用 INT8）')
    p.add_argument('--image',   required=True, help='测试图片路径')
    p.add_argument('--conf',    type=float, default=0.4,  help='置信度阈值')
    p.add_argument('--iou',     type=float, default=0.45, help='NMS IoU 阈值')
    p.add_argument('--names',   nargs='+',  default=['stain'])
    p.add_argument('--save',    action='store_true', help='保存结果图片')
    p.add_argument('--no-show', action='store_true', help='不弹出显示窗口')
    return p.parse_args()


if __name__ == '__main__':
    args = parse_args()

    if args.fp32:
        model_path = '../02_onnx_export/output/best.onnx'
        print('[模式] FP32 ONNX（基准精度）')
    else:
        model_path = args.model
        print('[模式] INT8 ONNX（量化版，最终烧录到 P4 的版本）')

    run_inference(
        model_path  = model_path,
        image_path  = args.image,
        conf_thresh = args.conf,
        nms_iou     = args.iou,
        class_names = args.names,
        save        = args.save,
        show        = not args.no_show,
    )
