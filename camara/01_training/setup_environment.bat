@echo off
chcp 65001 >nul
echo ============================================================
echo TinyMirrorDet 环境安装脚本
echo ============================================================
echo.

REM 检查 Python 是否可用
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未找到 Python，请先安装 Python 3.9+
    echo 下载地址: https://www.python.org/downloads/
    pause
    exit /b 1
)

echo [1/4] 升级 pip ...
python -m pip install --upgrade pip

echo.
echo [2/4] 安装 PyTorch (CPU 版本，无需 GPU)...
echo       如果你有 NVIDIA GPU，请关闭此窗口，手动运行:
echo       pip install torch torchvision --index-url https://download.pytorch.org/whl/cu121
echo.
python -m pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu

echo.
echo [3/4] 安装其他依赖 ...
python -m pip install onnx onnxruntime opencv-python PyYAML tqdm matplotlib numpy scipy Pillow

echo.
echo [4/4] 验证安装 ...
python -c "import torch; import onnx; import onnxruntime; import cv2; print('所有依赖安装成功！')"

if %errorlevel% equ 0 (
    echo.
    echo ============================================================
    echo 安装完成！现在可以运行:
    echo   cd 01_training
    echo   python check_model_size.py    ^(验证模型架构^)
    echo   python train.py               ^(开始训练^)
    echo ============================================================
) else (
    echo.
    echo [错误] 某些依赖安装失败，请检查网络连接后重试。
)

echo.
pause
