# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 sbkong
"""Maintainer tool: ChordMini 2E1D checkpoint (MIT) -> ONNX for the app (copy-spec.md section 4 "코드").

Clones the official repository (ptnghia-j/ChordMini, MIT, weights in-tree), rebuilds the ChordNet 2E1D student
from checkpoints/2e1d_model_best.pth with the repository's own loader, exports it to ONNX (opset 17, dynamic
batch, the (x - mean) / std normalisation folded into the graph), checks ONNX vs PyTorch parity, cross-checks
against the third-party musetric export when it sits next to the output, and writes the CQT reference fixture
the C++ test compares its own CQT against (librosa 0.11.0, the same call the training data pipeline made).
The outputs go to <app>/build/_downloads/chordmini/release/ for upload to the GitHub release named in the
manifest; the app downloads the model from there at first use (states.md section 2).

    D:\\cc_venv\\Scripts\\python.exe tools\\models\\export_chordmini.py

Needs: git, torch (CPU is fine), onnx, onnxruntime, librosa 0.11.0, numpy.
"""
import hashlib
import json
import subprocess
import sys
from datetime import date
from pathlib import Path

import librosa
import numpy as np
import onnxruntime
import torch

REPO_URL = "https://github.com/ptnghia-j/ChordMini.git"
RELEASE_TAG = "models-chordmini-1"  # GitHub release of sbkong/copycoach that hosts the file
APP = Path(__file__).resolve().parents[2]
OUT = APP / "build" / "_downloads" / "chordmini"
REPO = OUT / "repo"
RELEASE = OUT / "release"
CHECKPOINT = "checkpoints/2e1d_model_best.pth"
SAMPLE_RATE = 22050
HOP = 2048
N_BINS = 144
BINS_PER_OCTAVE = 24
FMIN = float(librosa.note_to_hz("C1"))
SEQ_LEN = 108
N_CLASSES = 170
SMOOTHING_KERNEL = 9
LOG_FLOOR = 1e-6
OPSET = 17


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def clone() -> None:
    if (REPO / "LICENSE").exists():
        return
    OUT.mkdir(parents=True, exist_ok=True)
    print("cloning ChordMini ...")
    subprocess.run(["git", "clone", "--depth", "1", REPO_URL, str(REPO)], check=True)


class Wrapper(torch.nn.Module):
    """Normalisation in the graph, logits only (the model returns (logits, features))."""

    def __init__(self, model: torch.nn.Module, mean: float, std: float):
        super().__init__()
        self.model = model
        self.mean = mean
        self.std = std

    def forward(self, x: torch.Tensor):
        x = (x - self.mean) / (self.std + 1e-8)
        return self.model(x)[0]


def load():
    """The repository's own reconstruction: architecture inferred from the state dict, stats from the file."""
    sys.path.insert(0, str(REPO))
    from src.models.chord_net import ChordNet  # noqa: E402
    from src.models.common.checkpoint_loading import _infer_chordnet  # noqa: E402
    from src.utils.checkpoint_utils import extract_model_state_dict, extract_normalization_stats  # noqa: E402
    from src.utils.chords import idx2voca_chord  # noqa: E402

    path = REPO / CHECKPOINT
    try:
        checkpoint = torch.load(path, map_location="cpu", weights_only=True)
    except Exception:  # the file carries a config dict next to the tensors
        checkpoint = torch.load(path, map_location="cpu", weights_only=False)
    state_dict = extract_model_state_dict(checkpoint)
    mean, std = extract_normalization_stats(checkpoint)
    architecture = _infer_chordnet(state_dict, None)
    config = checkpoint.get("config", {})
    if isinstance(config, dict):
        for key in architecture:
            if key in config:
                architecture[key] = config[key]
    model = ChordNet(**architecture)
    model.load_state_dict(state_dict)
    vocab = [idx2voca_chord()[i] for i in range(N_CLASSES)]
    print(f"{path.name}: {path.stat().st_size} bytes sha256 {sha256(path)}")
    print(f"architecture {architecture}  mean {mean:.6f} std {std:.6f}")
    return Wrapper(model, mean, std).eval(), architecture, mean, std, vocab


def export(wrapper: torch.nn.Module) -> Path:
    RELEASE.mkdir(parents=True, exist_ok=True)
    onnx_path = RELEASE / "chordmini-2e1d.onnx"
    dummy = torch.randn(1, SEQ_LEN, N_BINS)
    torch.onnx.export(
        wrapper,
        dummy,
        str(onnx_path),
        input_names=["features"],
        output_names=["logits"],
        dynamic_axes={"features": {0: "batch"}, "logits": {0: "batch"}},
        opset_version=OPSET,
        dynamo=False,
    )
    return onnx_path


def fake_features(rng: np.random.Generator, batch: int, mean: float, std: float) -> np.ndarray:
    return (mean + std * rng.standard_normal((batch, SEQ_LEN, N_BINS))).astype(np.float32)


def parity(onnx_path: Path, wrapper: torch.nn.Module, mean: float, std: float) -> float:
    """Max |ONNX - PyTorch| for batch 1 and 3 (the dynamic axis)."""
    session = onnxruntime.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    rng = np.random.default_rng(1)
    worst = 0.0
    for batch in (1, 3):
        x = fake_features(rng, batch, mean, std)
        with torch.inference_mode():
            ref = wrapper(torch.from_numpy(x)).numpy()
        (out,) = session.run(None, {"features": x})
        assert out.shape == (batch, SEQ_LEN, N_CLASSES), out.shape
        worst = max(worst, float(np.abs(out - ref).max()))
    return worst


def cross_check(onnx_path: Path, mean: float, std: float):
    """Against musetric/chordmini-onnx (fixed batch 16) when it was downloaded next to the output."""
    other = OUT / "chordnet.onnx"
    if not other.exists():
        return None
    ours = onnxruntime.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    theirs = onnxruntime.InferenceSession(str(other), providers=["CPUExecutionProvider"])
    x = fake_features(np.random.default_rng(2), 16, mean, std)
    (a,) = ours.run(None, {"features": x})
    (b,) = theirs.run(None, {"features": x})
    return {"max_abs_logit_diff": float(np.abs(a - b).max()),
            "argmax_agreement": float(np.mean(a.argmax(-1) == b.argmax(-1)))}


def host_cqt(y: np.ndarray) -> np.ndarray:
    """The recipe the C++ side follows (librosa.cqt semantics without its octave-by-octave approximation).

    Bin k at f_k = fmin * 2^(k / 24) takes a periodic Hann window of N_k = Q * sr / f_k samples (Q = 1 / alpha,
    alpha = (2^(2/24) - 1) / (2^(2/24) + 1), librosa.filters._relative_bandwidth), the complex kernel is
    L1-normalised (norm=1) and the response scaled by sqrt(N_k) (scale=True); frames are centred on m * hop
    over a zero-padded signal (center=True, pad_mode="constant"). Output log(|C| + 1e-6), [frames, bins].
    """
    r = 2.0 ** (1.0 / BINS_PER_OCTAVE)
    alpha = (r * r - 1.0) / (r * r + 1.0)
    q = 1.0 / alpha
    frames = 1 + len(y) // HOP
    out = np.zeros((frames, N_BINS), dtype=np.float64)
    for k in range(N_BINS):
        freq = FMIN * r ** k
        length = q * SAMPLE_RATE / freq
        offsets = np.arange(-length // 2, length // 2, dtype=np.float64)  # ceil(length) samples, as librosa
        n = len(offsets)
        window = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(n) / n)  # periodic Hann
        kernel = window * np.exp(-2j * np.pi * freq * offsets / SAMPLE_RATE)
        kernel /= np.abs(kernel).sum()
        first = int(offsets[0])
        for m in range(frames):
            start = m * HOP + first
            lo, hi = max(start, 0), min(start + n, len(y))
            if hi <= lo:
                continue
            seg = y[lo:hi]
            out[m, k] = np.sqrt(length) * np.abs(np.dot(kernel[lo - start:hi - start], seg))
    return np.log(out + LOG_FLOOR).astype(np.float32)


def fixture_signal() -> np.ndarray:
    """3 s of an E major chord (E2 B2 E3 G#3 B3 E4) with a slow decay and a little noise."""
    rng = np.random.default_rng(7)
    t = np.arange(SAMPLE_RATE * 3) / SAMPLE_RATE
    midi = [40, 47, 52, 56, 59, 64]
    signal = np.zeros_like(t)
    for i, note in enumerate(midi):
        hz = 440.0 * 2.0 ** ((note - 69) / 12.0)
        signal += (0.5 / (i + 1)) * np.sin(2 * np.pi * hz * t) * np.exp(-t / 4.0)
    signal += 0.01 * rng.standard_normal(t.size)
    return (signal / np.abs(signal).max() * 0.8).astype(np.float32)


def cqt_fixture(wrapper: torch.nn.Module, vocab: list[str]) -> dict:
    """librosa reference vs the host recipe on the fixture signal; both go through the model for a label check."""
    signal = fixture_signal()
    reference = librosa.cqt(signal, sr=SAMPLE_RATE, hop_length=HOP, fmin=FMIN, n_bins=N_BINS,
                            bins_per_octave=BINS_PER_OCTAVE, tuning=0.0)
    reference = np.log(np.abs(reference) + LOG_FLOOR).T.astype(np.float32)
    ours = host_cqt(signal)
    frames = min(len(reference), len(ours))
    reference, ours = reference[:frames], ours[:frames]
    diff = np.abs(ours - reference)
    fixture_dir = APP / "tests" / "fixtures" / "predict"
    fixture_dir.mkdir(parents=True, exist_ok=True)
    signal.tofile(fixture_dir / "cqt-signal.f32")
    reference.tofile(fixture_dir / "cqt-reference.f32")

    def labels(features: np.ndarray) -> list[str]:
        window = np.zeros((1, SEQ_LEN, N_BINS), dtype=np.float32)
        window[0, :frames] = features
        with torch.inference_mode():
            logits = wrapper(torch.from_numpy(window))[0]
        smoothed = torch.nn.functional.avg_pool1d(logits.T.unsqueeze(0), SMOOTHING_KERNEL, stride=1,
                                                  padding=SMOOTHING_KERNEL // 2).squeeze(0).T
        return [vocab[int(i)] for i in smoothed.argmax(-1)[:frames]]

    ref_labels, our_labels = labels(reference), labels(ours)
    (fixture_dir / "chordmini-labels.txt").write_text("\n".join(ref_labels) + "\n", encoding="utf-8")
    return {"frames": int(frames), "max_abs_diff": float(diff.max()), "mean_abs_diff": float(diff.mean()),
            "label_agreement": float(np.mean([a == b for a, b in zip(ref_labels, our_labels)])),
            "reference_labels": sorted(set(ref_labels)), "fixture_dir": str(fixture_dir)}


def main() -> None:
    clone()
    wrapper, architecture, mean, std, vocab = load()
    onnx_path = export(wrapper)
    worst = parity(onnx_path, wrapper, mean, std)
    print(f"{onnx_path.name}: {onnx_path.stat().st_size} bytes, onnx vs torch max abs diff {worst:.3e}")
    cross = cross_check(onnx_path, mean, std)
    print(f"vs musetric export: {cross}")
    cqt = cqt_fixture(wrapper, vocab)
    print(f"host CQT vs librosa: {cqt}")
    manifest = {
        "license": "MIT (ptnghia-j/ChordMini code and weights; export script sbkong)",
        "release_tag": RELEASE_TAG,
        "exported": date.today().isoformat(),
        "torch": torch.__version__, "librosa": librosa.__version__, "onnx_opset": OPSET,
        "source_checkpoint": CHECKPOINT, "source_sha256": sha256(REPO / CHECKPOINT),
        "architecture": architecture, "normalization": {"mean": mean, "std": std, "in_graph": True},
        "preprocessing": {"sample_rate": SAMPLE_RATE, "hop": HOP, "n_bins": N_BINS, "bins_per_octave": BINS_PER_OCTAVE,
                          "fmin_hz": FMIN, "log": "log(|cqt| + 1e-6)", "seq_len": SEQ_LEN,
                          "tuning": "librosa estimate_tuning in training; the app passes its own cents estimate"},
        "postprocessing": {"smoothing_kernel": SMOOTHING_KERNEL, "decode": "argmax per frame, runs merged"},
        "vocab": vocab,
        "parity_max_abs_diff": worst, "musetric_cross_check": cross, "cqt_fixture": cqt,
        "files": {onnx_path.name: {"size": onnx_path.stat().st_size, "sha256": sha256(onnx_path)}},
    }
    (RELEASE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"manifest -> {RELEASE / 'manifest.json'}")


if __name__ == "__main__":
    main()
