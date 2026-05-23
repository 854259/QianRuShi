"""
模型尺寸检查工具

运行后打印：
  - 总参数量
  - FP32 文件大小（训练/导出时）
  - INT8 估算大小（量化后烧录进 P4 时）
  - 各层参数明细
  - 通过/失败判断（是否满足 <1MB INT8 约束）

用法:
    cd 01_training
    python check_model_size.py
    python check_model_size.py --checkpoint outputs/weights/best.pt  # 检查已训练权重
"""

import sys
import argparse
import torch
from pathlib import Path

ROOT = Path(__file__).parent
sys.path.insert(0, str(ROOT))

from models.detector import TinyMirrorDet


def check_size(checkpoint_path=None, num_classes=1, verbose=True):
    model = TinyMirrorDet(num_classes=num_classes)

    if checkpoint_path and Path(checkpoint_path).exists():
        ckpt = torch.load(checkpoint_path, map_location='cpu')
        model.load_state_dict(ckpt.get('model', ckpt))
        print(f'[Size Check] 加载权重: {checkpoint_path}')
    else:
        print('[Size Check] 使用随机初始化权重（未加载 checkpoint）')

    total_params = sum(p.numel() for p in model.parameters())
    fp32_kb = total_params * 4 / 1024
    int8_kb  = total_params / 1024

    print(f'\n{"="*55}')
    print(f'TinyMirrorDet 模型尺寸报告')
    print(f'{"="*55}')
    print(f'  总参数量  : {total_params:,}')
    print(f'  FP32 大小 : {fp32_kb:.1f} KB  ({fp32_kb/1024:.2f} MB)')
    print(f'  INT8 大小 : {int8_kb:.1f} KB  ({int8_kb/1024:.2f} MB)  ← 烧录进 P4')
    print()

    # 约束检查
    limit_kb = 1024   # 1 MB = 1024 KB
    if int8_kb <= limit_kb:
        print(f'  [PASS] INT8 大小 {int8_kb:.1f} KB <= 1024 KB 约束 OK')
    else:
        print(f'  [FAIL] INT8 大小 {int8_kb:.1f} KB > 1024 KB 约束 NG')
        print(f'         请减少模型宽度/深度后重新检查')

    # 各层参数明细
    if verbose:
        print(f'\n  各模块参数明细:')
        print(f'  {"模块":<35} {"参数数量":>10} {"大小(KB)":>10}')
        print(f'  {"-"*57}')
        for name, module in model.named_children():
            n = sum(p.numel() for p in module.parameters())
            print(f'  {name:<35} {n:>10,} {n/1024:>9.1f}')
        print(f'  {"-"*57}')
        print(f'  {"合计":<35} {total_params:>10,} {fp32_kb:>9.1f} (FP32)')

    print(f'{"="*55}')

    return total_params, fp32_kb, int8_kb


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--checkpoint', default=None)
    p.add_argument('--classes',    type=int, default=1)
    p.add_argument('--no-detail',  action='store_true')
    return p.parse_args()


if __name__ == '__main__':
    args = parse_args()
    check_size(args.checkpoint, args.classes, verbose=not args.no_detail)
