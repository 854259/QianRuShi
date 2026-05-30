"""
电脑摄像头实时检测：浏览器显示版。

这个版本不使用 cv2.imshow，适合当前 OpenCV 没有窗口功能的环境。

用法:
    cd 04_pc_inference
    python demo_webcam_browser.py --conf 0.05

然后浏览器打开:
    http://127.0.0.1:8765

停止：在 PowerShell 里按 Ctrl+C。
"""

import argparse
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import cv2
import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[1]
TRAINING_ROOT = ROOT / "01_training"
sys.path.insert(0, str(TRAINING_ROOT))

from models.detector import TinyMirrorDet, decode_predictions  # noqa: E402

INPUT_SIZE = 128
MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD = np.array([0.229, 0.224, 0.225], dtype=np.float32)

latest_jpeg = None
latest_lock = threading.Lock()
stop_event = threading.Event()


def letterbox_rgb(img_rgb: np.ndarray, target: int = INPUT_SIZE):
    h, w = img_rgb.shape[:2]
    scale = target / max(h, w)
    nh, nw = int(h * scale), int(w * scale)
    resized = cv2.resize(img_rgb, (nw, nh), interpolation=cv2.INTER_LINEAR)

    top = (target - nh) // 2
    bottom = target - nh - top
    left = (target - nw) // 2
    right = target - nw - left
    padded = cv2.copyMakeBorder(
        resized, top, bottom, left, right,
        cv2.BORDER_CONSTANT, value=(114, 114, 114),
    )
    return padded, scale, (left, top)


def preprocess_frame(frame_bgr: np.ndarray, device: torch.device):
    img_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
    img_lb, scale, pad = letterbox_rgb(img_rgb)
    img = img_lb.astype(np.float32) / 255.0
    img = (img - MEAN) / STD
    tensor = torch.from_numpy(img.transpose(2, 0, 1)).unsqueeze(0).float().to(device)
    return tensor, scale, pad


def load_model(checkpoint: Path, device: torch.device):
    model = TinyMirrorDet(num_classes=1).to(device)
    ckpt = torch.load(checkpoint, map_location=device)
    model.load_state_dict(ckpt.get("model", ckpt))
    model.eval()
    epoch = ckpt.get("epoch", "?") if isinstance(ckpt, dict) else "?"
    best_map = ckpt.get("best_map", 0.0) if isinstance(ckpt, dict) else 0.0
    return model, epoch, best_map


def draw_detections(frame: np.ndarray, detections: torch.Tensor, scale: float, pad: tuple):
    h0, w0 = frame.shape[:2]
    pad_l, pad_t = pad
    for det in detections:
        x1, y1, x2, y2, score, _cls_id = det.tolist()
        x1 = int((x1 - pad_l) / scale)
        y1 = int((y1 - pad_t) / scale)
        x2 = int((x2 - pad_l) / scale)
        y2 = int((y2 - pad_t) / scale)
        x1 = max(0, min(w0 - 1, x1))
        y1 = max(0, min(h0 - 1, y1))
        x2 = max(0, min(w0 - 1, x2))
        y2 = max(0, min(h0 - 1, y2))
        if x2 <= x1 or y2 <= y1:
            continue
        cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 0, 255), 2)
        cv2.putText(
            frame, f"stain {score:.2f}", (x1, max(22, y1 - 8)),
            cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 0, 255), 2, cv2.LINE_AA,
        )


def camera_loop(args):
    global latest_jpeg

    checkpoint = (Path(__file__).parent / args.checkpoint).resolve()
    if not checkpoint.exists():
        raise FileNotFoundError(f"找不到权重文件: {checkpoint}")

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model, epoch, best_map = load_model(checkpoint, device)
    print(f"[Model] {checkpoint}")
    print(f"[Model] device={device} epoch={epoch} best_map={best_map:.4f}")

    cap = cv2.VideoCapture(args.camera, cv2.CAP_DSHOW)
    if not cap.isOpened():
        cap = cv2.VideoCapture(args.camera)
    if not cap.isOpened():
        raise RuntimeError(f"无法打开摄像头: {args.camera}")

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)

    fps_samples = []
    with torch.no_grad():
        while not stop_event.is_set():
            ok, frame = cap.read()
            if not ok:
                time.sleep(0.05)
                continue

            t0 = time.time()
            tensor, scale, pad = preprocess_frame(frame, device)
            raw = model(tensor)
            dets = decode_predictions(
                raw,
                anchors=TinyMirrorDet.ANCHORS,
                input_size=INPUT_SIZE,
                stride=TinyMirrorDet.STRIDE,
                conf_thresh=args.conf,
                nms_iou=args.iou,
            )[0].detach().cpu()
            draw_detections(frame, dets, scale, pad)

            fps = 1.0 / max(time.time() - t0, 1e-6)
            fps_samples = (fps_samples + [fps])[-30:]
            avg_fps = sum(fps_samples) / len(fps_samples)
            cv2.putText(
                frame, f"FPS {avg_fps:.1f}  detections {len(dets)}  conf {args.conf:.2f}",
                (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (255, 255, 0), 2, cv2.LINE_AA,
            )

            ok, jpg = cv2.imencode(".jpg", frame, [int(cv2.IMWRITE_JPEG_QUALITY), 82])
            if ok:
                with latest_lock:
                    latest_jpeg = jpg.tobytes()

    cap.release()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, _fmt, *_args):
        return

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            body = b"""<!doctype html>
<html><head><meta charset="utf-8"><title>Mirror Stain Detection</title>
<style>body{margin:0;background:#111;color:#eee;font-family:Arial,sans-serif}header{padding:10px 14px;background:#222}img{display:block;width:100vw;height:calc(100vh - 44px);object-fit:contain;background:#000}</style>
</head><body><header>Mirror Stain Detection Live - stop with Ctrl+C in PowerShell</header><img src="/stream"></body></html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        if self.path != "/stream":
            self.send_error(404)
            return

        self.send_response(200)
        self.send_header("Age", "0")
        self.send_header("Cache-Control", "no-cache, private")
        self.send_header("Pragma", "no-cache")
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
        self.end_headers()

        while not stop_event.is_set():
            with latest_lock:
                frame = latest_jpeg
            if frame is None:
                time.sleep(0.05)
                continue
            try:
                self.wfile.write(b"--frame\r\n")
                self.wfile.write(b"Content-Type: image/jpeg\r\n")
                self.wfile.write(f"Content-Length: {len(frame)}\r\n\r\n".encode("ascii"))
                self.wfile.write(frame)
                self.wfile.write(b"\r\n")
                time.sleep(0.03)
            except (BrokenPipeError, ConnectionResetError):
                break


def main():
    parser = argparse.ArgumentParser(description="浏览器实时查看污渍检测效果")
    parser.add_argument("--checkpoint", default="../01_training/outputs/weights/best.pt")
    parser.add_argument("--camera", type=int, default=0)
    parser.add_argument("--conf", type=float, default=0.05)
    parser.add_argument("--iou", type=float, default=0.45)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()

    worker = threading.Thread(target=camera_loop, args=(args,), daemon=True)
    worker.start()

    url = f"http://{args.host}:{args.port}"
    print(f"[Browser] 打开 {url}")
    print("[Stop] 在 PowerShell 按 Ctrl+C 停止")
    if not args.no_open:
        webbrowser.open(url)

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        server.server_close()


if __name__ == "__main__":
    main()
