"""
鎽勫儚澶村疄鏃舵娴嬫紨绀猴紙鍙€夛級

鐢ㄩ€旓細鐢ㄧ數鑴戞憚鍍忓ご瀵瑰噯闀滃瓙瀹炴椂娴嬭瘯妫€娴嬫晥鏋滐紝蹇€熼獙璇佹ā鍨嬫槸鍚︽甯稿伐浣溿€?娉ㄦ剰锛氭憚鍍忓ご鐗堟湰浠呯敤浜庢紨绀猴紝鏈€缁堥儴缃插埌 ESP32-P4 鏃朵娇鐢ㄦ憚鍍忓ご妯″潡銆?
鐢ㄦ硶:
    cd 04_pc_inference
    python demo_webcam.py
    python demo_webcam.py --camera 1  # 浣跨敤绗?2 涓憚鍍忓ご
    鎸?Q 閫€鍑?"""

import sys
import argparse
import numpy as np
import cv2
import onnxruntime as ort
from pathlib import Path

ANCHORS    = [[13, 13], [32, 32], [64, 64]]
INPUT_SIZE = 128
STRIDE     = 16
MEAN       = np.array([0.485, 0.456, 0.406], dtype=np.float32)
STD        = np.array([0.229, 0.224, 0.225], dtype=np.float32)


def sigmoid(x):
    return 1.0 / (1.0 + np.exp(-np.clip(x, -20, 20)))


def preprocess_frame(frame: np.ndarray):
    rgb    = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    h, w   = rgb.shape[:2]
    scale  = INPUT_SIZE / max(h, w)
    nh, nw = int(h * scale), int(w * scale)
    resized = cv2.resize(rgb, (nw, nh))

    pad_h = INPUT_SIZE - nh
    pad_w = INPUT_SIZE - nw
    top, left = pad_h // 2, pad_w // 2
    img = cv2.copyMakeBorder(resized, top, pad_h - top, left, pad_w - left,
                              cv2.BORDER_CONSTANT, value=(114, 114, 114))

    img_norm = img.astype(np.float32) / 255.0
    img_norm = (img_norm - MEAN) / STD
    tensor   = img_norm.transpose(2, 0, 1)[np.newaxis]

    return tensor, scale, (left, top)


def decode(raw, conf_thresh=0.35, nms_thresh=0.45):
    A = len(ANCHORS)
    C = raw.shape[1] // A - 5
    H = W = raw.shape[2]

    raw  = raw[0].reshape(A, 5 + C, H, W).transpose(0, 2, 3, 1)
    gy, gx = np.meshgrid(np.arange(H), np.arange(W), indexing='ij')
    grid = np.stack([gx, gy], axis=-1).astype(np.float32)
    anc  = np.array(ANCHORS, dtype=np.float32)

    xy   = (sigmoid(raw[..., :2]) + grid[None]) * STRIDE
    wh   = anc[:, None, None] * np.exp(np.clip(raw[..., 2:4], -4, 4))
    conf = sigmoid(raw[..., 4:5])
    cls  = sigmoid(raw[..., 5:]) if C > 0 else np.ones_like(conf)

    score  = (conf * cls).max(axis=-1, keepdims=True)
    cls_id = (conf * cls).argmax(axis=-1, keepdims=True).astype(np.float32)

    x1y1 = xy - wh / 2
    x2y2 = xy + wh / 2
    boxes = np.concatenate([x1y1, x2y2, score, cls_id], axis=-1).reshape(-1, 6)

    keep  = boxes[:, 4] > conf_thresh
    boxes = boxes[keep]
    if len(boxes) == 0:
        return np.zeros((0, 6), dtype=np.float32)

    # Simple NMS
    order = boxes[:, 4].argsort()[::-1]
    kept  = []
    while len(order) > 0:
        i = order[0]; kept.append(i)
        if len(order) == 1: break
        ix1 = np.maximum(boxes[i, 0], boxes[order[1:], 0])
        iy1 = np.maximum(boxes[i, 1], boxes[order[1:], 1])
        ix2 = np.minimum(boxes[i, 2], boxes[order[1:], 2])
        iy2 = np.minimum(boxes[i, 3], boxes[order[1:], 3])
        inter = np.maximum(0, ix2 - ix1) * np.maximum(0, iy2 - iy1)
        aa = (boxes[i,2]-boxes[i,0]) * (boxes[i,3]-boxes[i,1])
        ab = (boxes[order[1:],2]-boxes[order[1:],0]) * (boxes[order[1:],3]-boxes[order[1:],1])
        iou   = inter / (aa + ab - inter + 1e-7)
        order = order[1:][iou < nms_thresh]
    return boxes[kept]


def draw(frame, dets, scale, pad, class_names):
    h0, w0 = frame.shape[:2]
    pad_l, pad_t = pad
    for det in dets:
        x1, y1, x2, y2, score, cls_id = det
        x1_o = int((x1 - pad_l) / scale)
        y1_o = int((y1 - pad_t) / scale)
        x2_o = int((x2 - pad_l) / scale)
        y2_o = int((y2 - pad_t) / scale)
        cx   = (x1_o + x2_o) // 2
        cy   = (y1_o + y2_o) // 2
        name = class_names[int(cls_id)] if int(cls_id) < len(class_names) else 'stain'

        cv2.rectangle(frame, (x1_o, y1_o), (x2_o, y2_o), (0, 255, 0), 2)
        cv2.drawMarker(frame, (cx, cy), (0, 0, 255), cv2.MARKER_CROSS, 16, 2)
        cv2.putText(frame, f'{name} {score:.2f}  ({cx},{cy})',
                    (x1_o, max(0, y1_o - 6)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 0), 1, cv2.LINE_AA)
    return frame


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--model',   default='../03_quantization/output/best_int8.onnx')
    p.add_argument('--camera',  type=int, default=0)
    p.add_argument('--conf',    type=float, default=0.35)
    p.add_argument('--names',   nargs='+', default=['stain'])
    args = p.parse_args()

    if not Path(args.model).exists():
        print(f'[Error] 妯″瀷鏂囦欢涓嶅瓨鍦? {args.model}')
        print('璇峰厛瀹屾垚浠诲姟涓夌殑閲忓寲姝ラ銆?)
        return

    sess = ort.InferenceSession(args.model, providers=['CPUExecutionProvider'])
    name = sess.get_inputs()[0].name

    cap = cv2.VideoCapture(args.camera)
    if not cap.isOpened():
        print(f'[Error] 鏃犳硶鎵撳紑鎽勫儚澶?{args.camera}')
        return

    print(f'[鎽勫儚澶存紨绀篯 鎸?Q 閫€鍑? 妯″瀷: {args.model}')

    import time
    fps_list = []

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        t0 = time.time()
        tensor, scale, pad = preprocess_frame(frame)
        raw  = sess.run(None, {name: tensor})[0]
        dets = decode(raw, args.conf)
        frame = draw(frame, dets, scale, pad, args.names)

        fps = 1.0 / (time.time() - t0 + 1e-9)
        fps_list = fps_list[-30:] + [fps]
        avg_fps  = sum(fps_list) / len(fps_list)

        cv2.putText(frame, f'FPS: {avg_fps:.1f}  Dets: {len(dets)}',
                    (8, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)

        cv2.imshow('Mirror Stain Demo (Q to quit)', frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == '__main__':
    main()
