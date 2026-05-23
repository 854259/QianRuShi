"""
ESP-DL 专用导出工具

功能：将 INT8 ONNX 模型转换为可直接嵌入 ESP-IDF 工程的 C 头文件

生成文件：
  output/model_data.hpp  — 模型字节数组（嵌入 ROM）
  output/model_info.hpp  — 预处理常量（锚框、均值、标准差等）

在 ESP-IDF 工程中使用：
  #include "model_data.hpp"
  #include "model_info.hpp"

  // 加载模型
  dl::Model *model = new dl::Model(
      (const char *)model_data, sizeof(model_data), MALLOC_CAP_SPIRAM
  );

用法:
    cd 03_quantization
    python espdl_export.py
    python espdl_export.py --model output/best_int8.onnx --out-dir output/
"""

import argparse
from pathlib import Path


# ── C 头文件生成 ──────────────────────────────────────────────────────────────

def model_to_c_header(model_path: str, output_dir: str,
                       var_name: str = 'model_data') -> str:
    """将二进制模型文件转换为 C uint8_t 数组头文件"""
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    with open(model_path, 'rb') as f:
        data = f.read()

    n_bytes  = len(data)
    hpp_path = output_dir / f'{var_name}.hpp'

    with open(hpp_path, 'w', encoding='utf-8') as f:
        f.write(f'// 自动生成 — 来源: {Path(model_path).name}\n')
        f.write(f'// 模型大小: {n_bytes} bytes ({n_bytes/1024:.1f} KB)\n')
        f.write(f'// ！！请勿手动编辑！！\n\n')
        f.write(f'#pragma once\n')
        f.write(f'#include <stdint.h>\n\n')
        f.write(f'const uint32_t {var_name}_len = {n_bytes}U;\n\n')
        f.write(f'// 16 字节对齐，满足 ESP-DL DMA 要求\n')
        f.write(f'__attribute__((aligned(16)))\n')
        f.write(f'const uint8_t {var_name}[] = {{\n')

        for i in range(0, n_bytes, 16):
            chunk   = data[i:i+16]
            hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
            f.write(f'    {hex_str},\n')

        f.write('};\n')

    print(f'[ESP-DL] 模型数组 → {hpp_path}  ({n_bytes} bytes)')
    return str(hpp_path)


def write_model_info(output_dir: str,
                      input_size: int = 128,
                      stride: int = 16,
                      anchors=None,
                      num_classes: int = 1,
                      class_names=None) -> str:
    """写入模型元信息头文件（供 ESP-IDF C 代码使用）"""
    if anchors is None:
        anchors = [[13, 13], [32, 32], [64, 64]]
    if class_names is None:
        class_names = ['stain']

    output_dir = Path(output_dir)
    hpp_path   = output_dir / 'model_info.hpp'
    grid_size  = input_size // stride

    # 格式化锚框
    anc_vals = ', '.join(f'{{{a[0]}, {a[1]}}}' for a in anchors)
    names_str = ', '.join(f'"{n}"' for n in class_names)

    with open(hpp_path, 'w', encoding='utf-8') as f:
        f.write('// 模型推理配置常量 — 由 espdl_export.py 生成\n')
        f.write('#pragma once\n\n')
        f.write('#include <stdint.h>\n\n')

        f.write('// ── 输入规格 ────────────────────────────────\n')
        f.write(f'#define MODEL_INPUT_W     {input_size}\n')
        f.write(f'#define MODEL_INPUT_H     {input_size}\n')
        f.write(f'#define MODEL_INPUT_C     3\n\n')

        f.write('// ── 输出规格 ────────────────────────────────\n')
        f.write(f'#define MODEL_STRIDE      {stride}\n')
        f.write(f'#define MODEL_GRID_W      {grid_size}\n')
        f.write(f'#define MODEL_GRID_H      {grid_size}\n')
        f.write(f'#define MODEL_NUM_ANCHORS {len(anchors)}\n')
        f.write(f'#define MODEL_NUM_CLASSES {num_classes}\n\n')

        f.write('// ── 后处理阈值 ──────────────────────────────\n')
        f.write('#define MODEL_CONF_THRESH  0.40f\n')
        f.write('#define MODEL_NMS_THRESH   0.45f\n\n')

        f.write('// ── 图像归一化参数（ImageNet 均值/标准差）──\n')
        f.write('static const float MODEL_MEAN[3] = {0.485f, 0.456f, 0.406f};\n')
        f.write('static const float MODEL_STD[3]  = {0.229f, 0.224f, 0.225f};\n\n')

        f.write(f'// ── 锚框（{input_size}x{input_size} 像素空间）────────────\n')
        f.write(f'static const int MODEL_ANCHORS[{len(anchors)}][2] = {{{anc_vals}}};\n\n')

        f.write('// ── 类别名称 ────────────────────────────────\n')
        f.write(f'static const char* MODEL_CLASS_NAMES[{num_classes}] = {{{names_str}}};\n')

    print(f'[ESP-DL] 模型信息 → {hpp_path}')
    return str(hpp_path)


def export(int8_onnx: str, output_dir: str,
            num_classes: int = 1, class_names=None):
    """完整 ESP-DL 导出流程"""
    print('=' * 55)
    print('ESP-DL 导出 (INT8 ONNX → C 头文件)')
    print('=' * 55)

    model_hpp = model_to_c_header(int8_onnx, output_dir)
    info_hpp  = write_model_info(output_dir, num_classes=num_classes,
                                  class_names=class_names)

    print()
    print('[ESP-DL] 导出完成！')
    print(f'  生成文件:')
    print(f'    {model_hpp}')
    print(f'    {info_hpp}')
    print()
    print('  在 ESP-IDF 工程的 main/ 目录下:')
    print('    #include "model_data.hpp"')
    print('    #include "model_info.hpp"')
    print()
    print('  加载模型（main.cpp 示例）:')
    print('    #include "esp_dl_lib.h"')
    print('    dl::Model *model = new dl::Model(')
    print('        (const char *)model_data,')
    print('        sizeof(model_data),')
    print('        MALLOC_CAP_SPIRAM   // 使用 PSRAM')
    print('    );')
    print()
    print('  注意: 还需要在 ESP-IDF 中配置 ESP-DL 组件')
    print('  参考: https://github.com/espressif/esp-dl')
    print('=' * 55)


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--model',    default='output/best_int8.onnx')
    p.add_argument('--out-dir',  default='output/')
    p.add_argument('--classes',  type=int,   default=1)
    p.add_argument('--names',    nargs='+',  default=['stain'])
    return p.parse_args()


if __name__ == '__main__':
    args = parse_args()
    export(
        int8_onnx   = args.model,
        output_dir  = args.out_dir,
        num_classes = args.classes,
        class_names = args.names,
    )
