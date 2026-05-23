"""
任务二：PyTorch 模型 → ONNX 无损转换

输入:  01_training/outputs/weights/best.pt  (Float32 权重)
输出:  02_onnx_export/output/best.onnx      (Float32 ONNX 图)

ONNX 模型规格:
  - 输入:  images   float32 [1, 3, 128, 128]  RGB 归一化图像
  - 输出:  output   float32 [1, 18, 8, 8]     原始 logits
  - opset: 12（ESP-DL 工具链兼容性最好）

用法:
    cd 02_onnx_export
    python export_onnx.py
    python export_onnx.py --checkpoint ../01_training/outputs/weights/best.pt
"""

import sys
import argparse
import torch
import onnx
from pathlib import Path

# 将 01_training 加入路径（导入模型定义）
ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / '01_training'))

from models.detector import TinyMirrorDet


def export_onnx(checkpoint_path: str,
                output_path: str = None,
                img_size: int = 128,
                num_classes: int = 1,
                opset: int = 12) -> str:
    """
    将 PyTorch 权重转换为 ONNX 格式。

    Returns:
        str: 生成的 ONNX 文件路径
    """
    # ── 确定输出路径 ──────────────────────────────────────────────────
    ckpt_path = Path(checkpoint_path)
    if output_path is None:
        out_dir = Path(__file__).parent / 'output'
        out_dir.mkdir(exist_ok=True)
        output_path = str(out_dir / (ckpt_path.stem + '.onnx'))

    # ── 加载 PyTorch 模型 ─────────────────────────────────────────────
    device = torch.device('cpu')   # ONNX 导出在 CPU 上做
    model  = TinyMirrorDet(num_classes=num_classes)

    print(f'[Export] 加载权重: {checkpoint_path}')
    ckpt  = torch.load(checkpoint_path, map_location=device)
    state = ckpt.get('model', ckpt)  # 兼容有无 wrapper 的 checkpoint
    model.load_state_dict(state)
    model.eval()

    total = sum(p.numel() for p in model.parameters())
    print(f'[Export] 参数量: {total:,}  ({total*4/1024:.1f} KB FP32)')

    # ── 构造 dummy 输入 ───────────────────────────────────────────────
    dummy = torch.zeros(1, 3, img_size, img_size)

    # ── 导出 ONNX ─────────────────────────────────────────────────────
    print(f'[Export] 导出至: {output_path}  (opset={opset})')
    torch.onnx.export(
        model,
        dummy,
        output_path,
        opset_version   = opset,
        input_names     = ['images'],
        output_names    = ['output'],
        dynamic_axes    = None,    # 固定 batch=1，ESP-DL 需要固定形状
        do_constant_folding = True,
        verbose         = False,
    )

    # ── 验证 ONNX 模型合法性 ─────────────────────────────────────────
    model_onnx = onnx.load(output_path)
    onnx.checker.check_model(model_onnx)

    file_kb = Path(output_path).stat().st_size / 1024
    print(f'[Export] 文件大小: {file_kb:.1f} KB')
    print(f'[Export] ONNX 合法性校验: OK')
    print(f'\n[Export] 完成！下一步: cd ../03_quantization && python quantize_int8.py')

    return output_path


def parse_args():
    p = argparse.ArgumentParser(description='PyTorch → ONNX 导出')
    p.add_argument('--checkpoint', default='../01_training/outputs/weights/best.pt')
    p.add_argument('--output',     default=None)
    p.add_argument('--img-size',   type=int, default=128)
    p.add_argument('--classes',    type=int, default=1)
    p.add_argument('--opset',      type=int, default=12)
    return p.parse_args()


if __name__ == '__main__':
    args = parse_args()
    export_onnx(
        checkpoint_path = args.checkpoint,
        output_path     = args.output,
        img_size        = args.img_size,
        num_classes     = args.classes,
        opset           = args.opset,
    )
