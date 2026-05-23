"""
任务三核心脚本：FP32 ONNX → INT8 静态量化

原理：
  1. 用 50~100 张代表性图片（校准集）跑一遍 FP32 推理
  2. 记录每层激活值的分布（最大值、最小值）
  3. 根据分布将 float32 的权重和激活值映射到 int8 范围 [-128, 127]
  4. 输出新的 INT8 ONNX 模型

精度损耗说明：
  - 量化是有损压缩，mAP 通常下降 1~5%
  - 校准集越有代表性，损耗越小
  - 建议校准集包含各类型污渍（水滴/油脂/大面积）和干净镜子

用法:
    cd 03_quantization
    # 先把 50~100 张有代表性的照片复制到 calibration_data/ 目录
    python quantize_int8.py
    python quantize_int8.py --model ../02_onnx_export/output/best.onnx
"""

import os
import argparse
import numpy as np
import cv2
from pathlib import Path

from onnxruntime.quantization import (
    quantize_static,
    CalibrationDataReader,
    QuantType,
    QuantFormat,
    CalibrationMethod,
)


# 与训练时保持完全一致的归一化参数
MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD  = np.array([0.229, 0.224, 0.225], dtype=np.float32)


class MirrorCalibrationReader(CalibrationDataReader):
    """
    为量化器提供校准数据。

    校准集要求：
    - 50~100 张图片（无需标注）
    - 包含各种污渍类型和干净镜子
    - 光线条件尽量多样
    - 直接放入 calibration_data/ 目录即可
    """

    def __init__(self, calib_dir: str, img_size: int = 128,
                 max_samples: int = 100):
        self.img_size = img_size
        self._data    = self._load(calib_dir, max_samples)
        self._index   = 0

    def _load(self, calib_dir: str, max_n: int) -> list:
        VALID_EXTS = {'.jpg', '.jpeg', '.png', '.bmp'}
        images = []

        calib_path = Path(calib_dir)
        if not calib_path.exists():
            raise FileNotFoundError(
                f'校准数据目录不存在: {calib_dir}\n'
                '请创建该目录并放入 50~100 张代表性图片。'
            )

        for fpath in sorted(calib_path.iterdir()):
            if fpath.suffix.lower() not in VALID_EXTS:
                continue
            img = cv2.imread(str(fpath))
            if img is None:
                continue
            img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
            img = cv2.resize(img, (self.img_size, self.img_size))
            img = img.astype(np.float32) / 255.0
            img = (img - MEAN) / STD
            img = img.transpose(2, 0, 1)[np.newaxis]    # (1, 3, H, W)
            images.append(img)
            if len(images) >= max_n:
                break

        n = len(images)
        if n == 0:
            raise RuntimeError(
                f'在 {calib_dir} 中未找到图片。\n'
                '请至少放入 50 张代表性图片用于校准。'
            )
        if n < 20:
            print(f'[WARN] 只有 {n} 张校准图片，建议至少 50 张以保证量化精度。')
        else:
            print(f'[Calibration] 加载 {n} 张校准图片')

        return images

    def get_next(self):
        if self._index >= len(self._data):
            return None
        sample = {'images': self._data[self._index]}
        self._index += 1
        return sample


def quantize(fp32_onnx: str,
             calib_dir: str,
             output_path: str = None,
             img_size: int = 128,
             max_samples: int = 100):
    """
    静态 INT8 量化主函数。

    Args:
        fp32_onnx   : FP32 ONNX 模型路径
        calib_dir   : 校准图片目录
        output_path : 输出 INT8 ONNX 路径（默认自动命名）
        img_size    : 模型输入尺寸
        max_samples : 最大校准样本数
    """
    fp32_path = Path(fp32_onnx)
    if not fp32_path.exists():
        raise FileNotFoundError(f'FP32 模型不存在: {fp32_onnx}')

    if output_path is None:
        out_dir = Path(__file__).parent / 'output'
        out_dir.mkdir(exist_ok=True)
        output_path = str(out_dir / (fp32_path.stem + '_int8.onnx'))

    print('=' * 55)
    print('INT8 静态量化')
    print('=' * 55)
    print(f'  FP32 模型  : {fp32_onnx}')
    print(f'  校准数据   : {calib_dir}')
    print(f'  输出路径   : {output_path}')
    print(f'  最大样本数 : {max_samples}')
    print()

    # 构建校准数据读取器
    calib_reader = MirrorCalibrationReader(
        calib_dir   = calib_dir,
        img_size    = img_size,
        max_samples = max_samples,
    )

    # 执行量化
    print('[Quantize] 正在运行量化（需要 1~5 分钟）...')
    quantize_static(
        model_input             = fp32_onnx,
        model_output            = output_path,
        calibration_data_reader = calib_reader,
        quant_format            = QuantFormat.QDQ,    # QDQ 格式与 ESP-DL 兼容性最好
        per_channel             = True,               # 按通道量化，精度损耗更小
        weight_type             = QuantType.QInt8,
        activation_type         = QuantType.QInt8,
        calibrate_method        = CalibrationMethod.MinMax,
        optimize_model          = True,
    )

    # 统计压缩效果
    fp32_kb = fp32_path.stat().st_size / 1024
    int8_kb  = Path(output_path).stat().st_size / 1024
    ratio    = fp32_kb / (int8_kb + 1e-7)

    print()
    print('[Quantize] 量化完成！')
    print(f'  FP32 大小 : {fp32_kb:.1f} KB')
    print(f'  INT8 大小 : {int8_kb:.1f} KB')
    print(f'  压缩比    : {ratio:.1f}x')
    print()
    print('下一步（二选一）:')
    print('  1. 验证精度损耗: python evaluate_quantized.py')
    print('  2. 生成 ESP-DL C 头文件: python espdl_export.py')
    print('  3. 电脑仿真验证: cd ../04_pc_inference && python run_inference.py --image xxx.jpg')
    print('=' * 55)

    return output_path


def parse_args():
    p = argparse.ArgumentParser(description='FP32 ONNX → INT8 静态量化')
    p.add_argument('--model',   default='../02_onnx_export/output/best.onnx')
    p.add_argument('--calib',   default='calibration_data/')
    p.add_argument('--output',  default=None)
    p.add_argument('--samples', type=int, default=100, help='最大校准图片数')
    return p.parse_args()


if __name__ == '__main__':
    args = parse_args()
    quantize(
        fp32_onnx   = args.model,
        calib_dir   = args.calib,
        output_path = args.output,
        max_samples = args.samples,
    )
