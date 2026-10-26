"""Spike (a): htdemucs ONNX separation speed per execution provider.

Usage: python -I bench.py <model.onnx> <provider> [seconds] [--plugin <path.dll> --plugin-name <Name>]
provider: cpu | dml | cuda | webgpu
Prints: load time, warm-up chunk time, steady per-chunk time, total time for N seconds of audio, RTF.
Audio content does not affect runtime, so a deterministic noise signal stands in for a song.
"""
import argparse
import sys
import time

import numpy as np
import onnxruntime as ort

SAMPLE_RATE = 44100
N_SAMPLES = int(7.8 * SAMPLE_RATE)  # 343,980 per model input spec
OVERLAP = N_SAMPLES // 4
STRIDE = N_SAMPLES - OVERLAP

PROVIDERS = {
    "cpu": ["CPUExecutionProvider"],
    "dml": ["DmlExecutionProvider", "CPUExecutionProvider"],
    "cuda": ["CUDAExecutionProvider", "CPUExecutionProvider"],
    "webgpu": ["WebGpuExecutionProvider", "CPUExecutionProvider"],
}


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("model")
    p.add_argument("provider", choices=PROVIDERS)
    p.add_argument("seconds", nargs="?", type=float, default=180.0)
    p.add_argument("--plugin", help="plugin EP library path (ORT >= 1.30 plugin EPs)")
    p.add_argument("--plugin-name", default="WebGpuExecutionProvider")
    p.add_argument("--no-fusion", action="store_true", help="DML: disable graph fusion")
    p.add_argument("--gpu-vendor", default="NVIDIA", help="plugin EP: substring of the device vendor to use")
    args = p.parse_args()

    if args.provider == "cuda":
        ort.preload_dlls()  # locate pip-installed CUDA/cuDNN DLLs (nvidia-* packages)

    opts = ort.SessionOptions()
    opts.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    providers = PROVIDERS[args.provider]
    if args.plugin:
        ort.register_execution_provider_library(args.plugin_name, args.plugin)
        devices = [d for d in ort.get_ep_devices() if d.ep_name == args.plugin_name]
        print("plugin devices:", [(d.ep_name, d.device.type, d.device.vendor) for d in devices], flush=True)
        chosen = [d for d in devices if args.gpu_vendor.lower() in d.device.vendor.lower()] or devices
        print("using:", chosen[0].device.vendor, flush=True)
        opts.add_provider_for_devices(chosen[:1], {})  # WebGPU EP factory accepts one device at a time
        providers = None
    if args.provider == "dml":
        # DML EP docs: memory pattern and parallel execution are unsupported.
        opts.enable_mem_pattern = False
        opts.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        if args.no_fusion:
            opts.add_session_config_entry("ep.dml.disable_graph_fusion", "1")
    t0 = time.perf_counter()
    sess = ort.InferenceSession(args.model, sess_options=opts, providers=providers)
    load_s = time.perf_counter() - t0
    active = sess.get_providers()[0]
    print(f"ort {ort.__version__}  requested={args.provider}  active={active}  load={load_s:.2f}s", flush=True)
    if args.provider != "cpu" and active == "CPUExecutionProvider":
        print("ERROR: fell back to CPU", flush=True)
        return 1

    rng = np.random.default_rng(0)
    total = int(args.seconds * SAMPLE_RATE)
    mix = (rng.standard_normal((2, total), dtype=np.float32) * 0.1)
    n_chunks = max(1, (total + STRIDE - 1) // STRIDE)

    chunk_times = []
    t_all = time.perf_counter()
    for i in range(n_chunks):
        start = i * STRIDE
        chunk = mix[:, start:start + N_SAMPLES]
        if chunk.shape[1] < N_SAMPLES:
            chunk = np.pad(chunk, ((0, 0), (0, N_SAMPLES - chunk.shape[1])))
        t = time.perf_counter()
        out = sess.run(["stems"], {"mix": chunk[np.newaxis]})[0]
        chunk_times.append(time.perf_counter() - t)
    total_s = time.perf_counter() - t_all

    steady = float(np.median(chunk_times[1:])) if len(chunk_times) > 1 else chunk_times[0]
    print(f"output shape={out.shape} finite={bool(np.isfinite(out).all())}")
    print(f"chunks={n_chunks}  warmup={chunk_times[0]:.2f}s  steady/chunk={steady:.3f}s")
    print(f"audio={args.seconds:.0f}s  total={total_s:.1f}s  RTF={total_s / args.seconds:.3f}  "
          f"({args.seconds / total_s:.1f}x realtime)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
