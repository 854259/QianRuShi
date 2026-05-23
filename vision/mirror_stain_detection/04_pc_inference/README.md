# 任务四：电脑端仿真推理验证

## 目标
在把代码发给队友之前，先用你自己的电脑跑一遍，确认模型逻辑完全通，
在屏幕上看到污渍被框出来、(X,Y) 坐标被正确打印出来。

**所见即所得**——这里表现多准，P4 上就多准。

## 步骤

### 基础验证
```bash
cd 04_pc_inference

# 方法一：测试单张图片（最常用）
python run_inference.py --image test_images/your_photo.jpg

# 保存结果图片（不弹窗）
python run_inference.py --image test_images/your_photo.jpg --save --no-show

# 降低阈值（检测不到目标时尝试）
python run_inference.py --image test_images/your_photo.jpg --conf 0.25
```

### 与 FP32 基准对比
```bash
# FP32 版本（精度天花板）
python run_inference.py --image test_images/your_photo.jpg --fp32

# INT8 版本（量化后）
python run_inference.py --image test_images/your_photo.jpg
```

两个版本结果应基本一致，若 INT8 版本漏检明显，回到任务三改进校准集。

### 实时摄像头演示（可选）
```bash
python demo_webcam.py         # 使用默认摄像头
python demo_webcam.py --camera 1   # 第二个摄像头
```

## 解读输出

运行后控制台打印：
```
[Inference] 模型: ../03_quantization/output/best_int8.onnx  (112.4 KB)
[Inference] 图片: test_images/stain_01.jpg
[结果] 检测到 2 个目标:
  [stain]  置信度=0.847  中心坐标=(284.5, 193.2)  框=[201,121,368,265]
  [stain]  置信度=0.612  中心坐标=(521.3, 387.8)  框=[489,352,554,424]
```

含义解读：
- `置信度` = 0~1，越高越确定是污渍
- `中心坐标` = 污渍中心在原图的像素坐标 (X, Y)
  - 这正是 ESP32-P4 最终输出给控制系统的数据
- `框` = [x左, y上, x右, y下] 检测框四角坐标

图片窗口：
- 绿色矩形框 = 检测框
- 红色十字 = 中心坐标位置
- 坐标数字标注在十字旁边

## 放入 test_images/ 的图片
建议放入：
- 有污渍的镜子照片（主要测试）
- 干净的镜子（测试误检率）
- 极端情况：光线很暗/很亮、角度倾斜

## 常见问题
| 现象 | 原因 | 解决 |
|------|------|------|
| 检测到 0 个目标 | 阈值过高 | 加 `--conf 0.2` |
| 太多误检 | 阈值过低 | 加 `--conf 0.5` |
| 框不准确 | 量化损耗大 | 增加校准集，重跑任务三 |
| 模型文件不存在 | 未完成任务三 | 先运行 `quantize_int8.py` |
