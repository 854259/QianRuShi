"""
验证 ONNX 导出的无损性

比较 PyTorch 原始输出 vs ONNX Runtime 推理输出。
最大差值 < 1e-4 即认为导出无损。

用法:
    cd 02_onnx_export
    python verify_onnx.py
    python verify_onnx.py --image path/to/test.jpg  # 用真实图片验证
"""

import sys
import argparse
import numpy as np
import torch
import cv2
import onnxruntime as ort
from pathlib import Path

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / '01_training'))

from models.detector import TinyMirrorDet

MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD  = np.array([0.229, 0.224, 0.225], dtype=np.float32)


def preprocess_image(image_path: str, img_size: int = 128) -> np.ndarray:
    """加载并预处理图片 → (1, 3, H, W) float32"""
    img = cv2.imread(image_path)
    if img is None:
        raise FileNotFoundError(f'无法读取图片: {image_path}')
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    img = cv2.resize(img, (img_size, img_size))
    img = img.astype(np.float32) / 255.0
    img = (img - MEAN) / STD
    return img.transpose(2, 0, 1)[np.newaxis]   # (1, 3, H, W)


def run_pytorch(checkpoint_path: str, img_np: np.ndarray, num_classes: int = 1) -> np.ndarray:
    model = TinyMirrorDet(num_classes=num_classes)
    ckpt  = torch.load(checkpoint_path, map_location='cpu')
    model.load_state_dict(ckpt.get('model', ckpt))
    model.eval()
    with torch.no_grad():
        out = model(torch.from_numpy(img_np))
    return out.numpy()


def run_onnx(onnx_path: str, img_np: np.ndarray) -> np.ndarray:
    sess   = ort.InferenceSession(onnx_path, providers=['CPUExecutionProvider'])
    inputs = {sess.get_inputs()[0].name: img_np}
    return sess.run(None, inputs)[0]   # (1, 18, 8, 8)


def verify(checkpoint_path: str, onnx_path: str,
           image_path: str = None, num_classes: int = 1):
    print('=' * 55)
    print('ONNX 无损导出验证')
    print('=' * 55)

    # 构造测试输入
    if image_path:
        img_np = preprocess_image(image_path)
        print(f'测试图片: {image_path}')
    else:
        np.random.seed(42)
        img_np = np.random.randn(1, 3, 128, 128).astype(np.float32)
        print('测试输入: 随机噪声（传入 --image 可使用真实图片）')

    print(f'PyTorch  权重: {checkpoint_path}')
    print(f'ONNX    模型: {onnx_path}')
    print()

    # 分别推理
    pt_out   = run_pytorch(checkpoint_path, img_np, num_classes)
    onnx_out = run_onnx(onnx_path, img_np)

    # 对比
    max_diff  = float(np.abs(pt_out - onnx_out).max())
    mean_diff = float(np.abs(pt_out - onnx_out).mean())

    print(f'输出形状 (PyTorch): {pt_out.shape}')
    print(f'输出形状 (ONNX)  : {onnx_out.shape}')
    print()
    print(f'最大绝对误差 : {max_diff:.2e}')
    print(f'平均绝对误差 : {mean_diff:.2e}')
    print()

    if max_diff < 1e-4:
        print('[PASS] OK ONNX 与 PyTorch 输出几乎完全一致，导出无损！')
    elif max_diff < 1e-2:
        print('[WARN] ⚠ 存在轻微差异（浮点精度），对检测结果影响可忽略。')
    else:
        print('[FAIL] NG 输出差异过大，请检查 export_onnx.py 配置。')

    print('=' * 55)
    print()
    print('下一步: cd ../03_quantization && python quantize_int8.py')


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--checkpoint', default='../01_training/outputs/weights/best.pt')
    p.add_argument('--onnx',       default='output/best.onnx')
    p.add_argument('--image',      default=None)
    p.add_argument('--classes',    type=int, default=1)
    return p.parse_args()


if __name__ == '__main__':
    args = parse_args()
    verify(args.checkpoint, args.onnx, args.image, args.classes)
