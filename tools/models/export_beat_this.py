# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 sbkong
"""Maintainer tool: Beat This! checkpoints (CPJKU, MIT) -> ONNX for the app (copy-spec.md section 4).

Downloads the official checkpoints from the JKU cloud space, exports each to ONNX (opset 17, dynamic time
axis, inputs/outputs named for the C++ side), saves the log-mel filterbank the host needs, checks ONNX vs
PyTorch parity and the host-side spectrogram recipe against beat_this's own, and writes a manifest with
SHA-256 sums. The outputs go to <app>/build/_downloads/beat_this/release/ for upload to the GitHub release
named in the manifest; the app downloads them from there at first use (states.md section 2).

    D:\\cc_venv\\Scripts\\python.exe tools\\models\\export_beat_this.py            # final0 + small0
    D:\\cc_venv\\Scripts\\python.exe tools\\models\\export_beat_this.py small0     # one of them

Needs: torch (CPU is fine), torchaudio, onnx, onnxscript, onnxruntime, beat-this 1.1.0, numpy.
"""
import hashlib
import inspect
import json
import sys
from datetime import date
from pathlib import Path
from urllib.request import urlretrieve

import numpy as np
import onnxruntime
import torch
import torchaudio

from beat_this.model.beat_tracker import BeatThis
from beat_this.preprocessing import LogMelSpect
from beat_this.utils import replace_state_dict_key

CHECKPOINT_URL = "https://cloud.cp.jku.at/public.php/dav/files/7ik4RrBKTS273gp"
RELEASE_TAG = "models-beat-this-1"  # GitHub release of sbkong/copycoach that hosts the files
APP = Path(__file__).resolve().parents[2]
OUT = APP / "build" / "_downloads" / "beat_this"
RELEASE = OUT / "release"
SAMPLE_RATE = 22050
N_FFT = 1024
HOP = 441
N_MELS = 128
F_MIN = 30.0
F_MAX = 11000.0
LOG_MULTIPLIER = 1000.0
CHUNK = 1500
BORDER = 6
OPSET = 17


class Wrapper(torch.nn.Module):
    """ONNX wants a tuple, the model returns a dict."""

    def __init__(self, model: BeatThis):
        super().__init__()
        self.model = model

    def forward(self, x: torch.Tensor):
        out = self.model(x)
        return out["beat"], out["downbeat"]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def download(name: str) -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    dest = OUT / f"beat_this-{name}.ckpt"
    if not dest.exists():
        print(f"downloading {name}.ckpt ...")
        urlretrieve(f"{CHECKPOINT_URL}/{name}.ckpt", dest)
    print(f"{dest.name}: {dest.stat().st_size} bytes sha256 {sha256(dest)}")
    return dest


def load(ckpt: Path) -> BeatThis:
    checkpoint = torch.load(ckpt, map_location="cpu", weights_only=True)
    params = set(inspect.signature(BeatThis).parameters)
    hparams = {k: v for k, v in checkpoint["hyper_parameters"].items() if k in params}
    model = BeatThis(**hparams)
    model.load_state_dict(replace_state_dict_key(checkpoint["state_dict"], "model.", ""))
    return model.eval()


def export(name: str, model: BeatThis) -> Path:
    RELEASE.mkdir(parents=True, exist_ok=True)
    onnx_path = RELEASE / f"beat_this-{name}.onnx"
    wrapper = Wrapper(model).eval()
    dummy = torch.randn(1, CHUNK, N_MELS)
    torch.onnx.export(
        wrapper,
        dummy,
        str(onnx_path),
        input_names=["spectrogram"],
        output_names=["beat", "downbeat"],
        dynamic_axes={"spectrogram": {0: "batch", 1: "time"}, "beat": {0: "batch", 1: "time"}, "downbeat": {0: "batch", 1: "time"}},
        opset_version=OPSET,
        dynamo=False,
    )
    return onnx_path


def parity(onnx_path: Path, model: BeatThis) -> float:
    """Max |ONNX - PyTorch| over a full chunk and an odd-length clip (the dynamic axis)."""
    session = onnxruntime.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    worst = 0.0
    for frames in (CHUNK, 777):
        x = torch.randn(1, frames, N_MELS)
        with torch.inference_mode():
            ref = model(x)
        beat, downbeat = session.run(None, {"spectrogram": x.numpy()})
        worst = max(worst, float(np.abs(beat - ref["beat"].numpy()).max()), float(np.abs(downbeat - ref["downbeat"].numpy()).max()))
    return worst


def filterbank() -> Path:
    """torchaudio's mel matrix [513, 128], the same object LogMelSpect uses."""
    mel = torchaudio.transforms.MelSpectrogram(
        sample_rate=SAMPLE_RATE, n_fft=N_FFT, hop_length=HOP, f_min=F_MIN, f_max=F_MAX, n_mels=N_MELS,
        mel_scale="slaney", normalized="frame_length", power=1)
    RELEASE.mkdir(parents=True, exist_ok=True)
    fb = mel.mel_scale.fb.numpy().astype(np.float32)
    assert fb.shape == (N_FFT // 2 + 1, N_MELS), fb.shape
    path = RELEASE / "mel-filterbank.bin"
    fb.tofile(path)
    return path


def host_logmel(signal: np.ndarray, fb: np.ndarray) -> np.ndarray:
    """The recipe the C++ side follows: periodic Hann, centred reflect padding, |X| / sqrt(n_fft), mel, log1p."""
    window = np.hanning(N_FFT + 1)[:-1].astype(np.float32)  # periodic
    padded = np.pad(signal, N_FFT // 2, mode="reflect")
    frames = 1 + (len(padded) - N_FFT) // HOP
    out = np.zeros((frames, N_MELS), dtype=np.float32)
    for i in range(frames):
        seg = padded[i * HOP:i * HOP + N_FFT] * window
        mag = np.abs(np.fft.rfft(seg)) / np.sqrt(N_FFT)
        out[i] = np.log1p(LOG_MULTIPLIER * (mag.astype(np.float32) @ fb))
    return out


def spectrogram_parity(fb_path: Path) -> tuple[float, Path]:
    """Our recipe vs beat_this.LogMelSpect on a test signal; the frames go out as a fixture for the C++ test."""
    rng = np.random.default_rng(7)
    t = np.arange(SAMPLE_RATE * 2) / SAMPLE_RATE
    signal = (0.3 * np.sin(2 * np.pi * 220 * t) + 0.05 * rng.standard_normal(t.size)).astype(np.float32)
    for click in range(0, t.size, SAMPLE_RATE // 2):
        signal[click:click + 200] += 0.6
    ref = LogMelSpect()(torch.from_numpy(signal)).numpy()
    fb = np.fromfile(fb_path, dtype=np.float32).reshape(N_FFT // 2 + 1, N_MELS)
    ours = host_logmel(signal, fb)
    assert ours.shape == ref.shape, (ours.shape, ref.shape)
    fixture_dir = APP / "tests" / "fixtures" / "predict"
    fixture_dir.mkdir(parents=True, exist_ok=True)
    signal.tofile(fixture_dir / "logmel-signal.f32")
    ref.astype(np.float32).tofile(fixture_dir / "logmel-reference.f32")
    return float(np.abs(ours - ref).max()), fixture_dir


def main() -> None:
    names = sys.argv[1:] or ["final0", "small0"]
    manifest = {
        "license": "MIT (CPJKU/beat_this code and weights; export script sbkong)",
        "release_tag": RELEASE_TAG,
        "exported": date.today().isoformat(),
        "torch": torch.__version__, "onnx_opset": OPSET,
        "preprocessing": {"sample_rate": SAMPLE_RATE, "n_fft": N_FFT, "hop": HOP, "n_mels": N_MELS, "f_min": F_MIN,
                          "f_max": F_MAX, "window": "hann periodic", "center": "reflect", "magnitude_scale": "1/sqrt(n_fft)",
                          "log": "log1p(1000 * mel)", "chunk_frames": CHUNK, "border_frames": BORDER, "fps": SAMPLE_RATE / HOP},
        "postprocessing": {"peak_kernel": 7, "threshold_logit": 0.0, "dedup_width_frames": 1, "snap_downbeats": True},
        "files": {},
    }
    fb_path = filterbank()
    manifest["files"]["mel-filterbank.bin"] = {"size": fb_path.stat().st_size, "sha256": sha256(fb_path)}
    diff, fixture_dir = spectrogram_parity(fb_path)
    print(f"log-mel recipe vs LogMelSpect: max abs diff {diff:.3e}  (fixture -> {fixture_dir})")
    for name in names:
        ckpt = download(name)
        model = load(ckpt)
        onnx_path = export(name, model)
        worst = parity(onnx_path, model)
        print(f"{onnx_path.name}: {onnx_path.stat().st_size} bytes, onnx vs torch max abs diff {worst:.3e}")
        manifest["files"][onnx_path.name] = {"size": onnx_path.stat().st_size, "sha256": sha256(onnx_path),
                                             "source_checkpoint": ckpt.name, "source_sha256": sha256(ckpt),
                                             "parity_max_abs_diff": worst}
    (RELEASE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"manifest -> {RELEASE / 'manifest.json'}")


if __name__ == "__main__":
    main()
