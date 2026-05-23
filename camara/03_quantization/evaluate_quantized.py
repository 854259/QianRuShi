"""
量化精度评估：对比 FP32 vs INT8 模型在同一批图片上的输出差异

量化精度损耗判断标准：
  - 输出差异 < 0.1  → 优秀，几乎无损
  - 输出差异 0.1~0.5 → 正常，mAP 约下降 1~3%
  - 输出差异 > 0.5  → 偏大，考虑增加校准数据或使用 Entropy 校准法

用法:
    cd 03_quantization
    python evaluate_quantized.py
    python evaluate_quantized.py --fp32 ../02_onnx_export/output/best.onnx
                                 --int8 output/best_int8.onnx
                                 --images calibration_data/
"""

import os
import argparse
import numpy as np
import cv2
import onnxruntime as ort
from pathlib import Path


MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD  = np.array([0.229, 0.224, 0.225], dtype=np.float32)


def preprocess(img_path: str, img_size: int = 128) -> np.ndarray:
    img = cv2.imread(img_path)
    if img is None:
        return None
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    img = cv2.resize(img, (img_size, img_size))
    img = img.astype(np.float32) / 255.0
    img = (img - MEAN) / STD
    return img.transpose(2, 0, 1)[np.newaxis]


def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-np.clip(x, -20, 20)))


def run_session(onnx_path: str, img_np: np.ndarray) -> np.ndarray:
    sess = ort.InferenceSession(onnx_path, providers=['CPUExecutionProvider'])
    return sess.run(None, {sess.get_inputs()[0].name: img_np})[0]


def evaluate(fp32_path: str, int8_path: str, images_dir: str, img_size: int = 128):
    VALID_EXTS = {'.jpg', '.jpeg', '.png', '.bmp'}

    img_files = [
        str(p) for p in Path(images_dir).iterdir()
        if p.suffix.lower() in VALID_EXTS
    ][:50]   # 最多取 50 张

    if not img_files:
        print(f'[Error] 在 {images_dir} 中未找到图片，请指定有图片的目录。')
        return

    print(f'[Eval] 对比 {len(img_files)} 张图片的 FP32 vs INT8 输出...')
    print(f'  FP32: {fp32_path}')
    print(f'  INT8: {int8_path}')
    print()

    all_max_diff  = []
    all_mean_diff = []
    fp32_dets = 0
    int8_dets = 0
    CONF_THRESH = 0.25

    for fp in img_files:
        img_np = preprocess(fp, img_size)
        if img_np is None:
            continue

        fp32_out = run_session(fp32_path, img_np)
        int8_out = run_session(int8_path, img_np)

        max_diff  = float(np.abs(fp32_out - int8_out).max())
        mean_diff = float(np.abs(fp32_out - int8_out).mean())
        all_max_diff.append(max_diff)
        all_mean_diff.append(mean_diff)

        # 统计检测数量差异（正样本 objectness > 0.25）
        fp32_conf = sigmoid(fp32_out.reshape(fp32_out.shape[0], -1, fp32_out.shape[1] // 18, 18))
        # 简单统计 objectness > threshold 的格子数
        fp32_dets += (sigmoid(fp32_out[:, 4::6, :, :]) > CONF_THRESH).sum()
        int8_dets += (sigmoid(int8_out[:, 4::6, :, :]) > CONF_THRESH).sum()

    avg_max  = float(np.mean(all_max_diff))
    avg_mean = float(np.mean(all_mean_diff))

    print('=' * 55)
    print('量化精度评估报告')
    print('=' * 55)
    print(f'  测试图片数  : {len(all_max_diff)}')
    print(f'  平均最大误差: {avg_max:.4f}')
    print(f'  平均均值误差: {avg_mean:.4f}')
    print(f'  FP32 激活锚框数: {int(fp32_dets)}')
    print(f'  INT8 激活锚框数: {int(int8_dets)}')
    print()

    # 评级
    if avg_max < 0.10:
        grade = '优秀 ***  (量化几乎无损)'
    elif avg_max < 0.30:
        grade = '良好 **   (损耗可接受，mAP 约降 1~3%)'
    elif avg_max < 0.60:
        grade = '一般 *    (损耗偏大，建议增加校准数据)'
    else:
        grade = '较差 NG    (损耗过大！请检查校准集多样性)'

    print(f'  量化质量评级: {grade}')
    print()

    if avg_max > 0.30:
        print('[建议] 提升量化精度的方法:')
        print('  1. 增加校准图片（建议 100 张，包含各类污渍和干净场景）')
        print('  2. 在 quantize_int8.py 中将 CalibrationMethod 改为 Entropy')
        print('  3. 将 per_channel=True（已是默认值，精度更好）')

    print('=' * 55)


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--fp32',   default='../02_onnx_export/output/best.onnx')
    p.add_argument('--int8',   default='output/best_int8.onnx')
    p.add_argument('--images', default='calibration_data/', help='测试图片目录')
    return p.parse_args()


if __name__ == '__main__':
    args = parse_args()
    evaluate(args.fp32, args.int8, args.images)
