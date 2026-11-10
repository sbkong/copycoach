# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 sbkong
"""Maintainer tool: Spleeter 4stems (Deezer, MIT) -> ONNX for the app (copy-spec.md section 4 "스템").

Downloads the official 4stems checkpoint (deezer/spleeter release v1.4.0), rebuilds the four U-Nets in PyTorch
from the TensorFlow variables (spleeter/model/functions/unet.py: conv 5x5 stride 2 "same", BatchNorm eps 1e-3,
ELU for 4stems, deconv -> ELU -> BatchNorm, final 4x4 dilated conv with sigmoid times the input), folds the
mask formula (exponent 2, eps 1e-10, spleeter/model/__init__.py) into the graph and exports one ONNX file:
magnitude [splits, 2, 512, bins] -> masks [splits, 4, 2, 512, bins] for vocals, drums, bass, other. Checks
ONNX vs PyTorch, cross-checks the per-stem estimates against Best-Practice/spleeter-4stems-onnx when those
files sit in crosscheck/, and writes the fixture the C++ test reproduces end to end (STFT, masks, iSTFT in
numpy, the same recipe core/separation follows). Output: <app>/build/_downloads/spleeter/release/ for the
GitHub release named in the manifest; the app downloads it at first use (states.md section 2).

    D:\\cc_venv\\Scripts\\python.exe tools\\models\\export_spleeter.py

Needs: tensorflow (checkpoint reader only), torch, onnx, onnxruntime, numpy.
"""
import hashlib
import json
import tarfile
from datetime import date
from pathlib import Path
from urllib.request import urlretrieve

import numpy as np
import onnxruntime
import torch

RELEASE_TAG = "models-spleeter-1"  # GitHub release of sbkong/copycoach that hosts the file
CHECKPOINT_URL = "https://github.com/deezer/spleeter/releases/download/v1.4.0/4stems.tar.gz"
CHECKPOINT_SHA256 = "3adb4a50ad4eb18c7c4d65fcf4cf2367a07d48408a5eb7d03cd20067429dfaa8"
APP = Path(__file__).resolve().parents[2]
OUT = APP / "build" / "_downloads" / "spleeter"
RELEASE = OUT / "release"
STEMS = ["vocals", "drums", "bass", "other"]  # instrument_list order of resources/4stems.json
SAMPLE_RATE = 44100
FRAME = 4096
HOP = 1024
T = 512
F_11K = 1024  # resources/4stems.json
F_16K = 1536  # resources/4stems-16kHz.json, the same weights
EPS = 1e-10
EXPONENT = 2
WINDOW_COMPENSATION = 2.0 / 3.0
FILTERS = [16, 32, 64, 128, 256, 512]
OPSET = 17


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def download() -> Path:
    OUT.mkdir(parents=True, exist_ok=True)
    tarball = OUT / "4stems.tar.gz"
    if not tarball.exists():
        print("downloading 4stems.tar.gz ...")
        urlretrieve(CHECKPOINT_URL, tarball)
    digest = sha256(tarball)
    assert digest == CHECKPOINT_SHA256, digest
    folder = OUT / "4stems"
    if not (folder / "model.index").exists():
        with tarfile.open(tarball) as tar:
            tar.extractall(folder, filter="data")
    return folder / "model"


class UNet(torch.nn.Module):
    """apply_unet with ELU (4stems): the same layer order, TF "same" padding written out."""

    def __init__(self):
        super().__init__()
        chans = [2] + FILTERS
        self.convs = torch.nn.ModuleList(torch.nn.Conv2d(chans[i], chans[i + 1], 5, stride=2) for i in range(6))
        self.conv_bns = torch.nn.ModuleList(torch.nn.BatchNorm2d(c, eps=1e-3) for c in FILTERS[:5])
        up_in = [512, 512, 256, 128, 64, 32]
        up_out = [256, 128, 64, 32, 16, 1]
        self.ups = torch.nn.ModuleList(torch.nn.ConvTranspose2d(i, o, 5, stride=2) for i, o in zip(up_in, up_out))
        self.up_bns = torch.nn.ModuleList(torch.nn.BatchNorm2d(c, eps=1e-3) for c in up_out)
        self.final = torch.nn.Conv2d(1, 2, 4, dilation=2, padding=3)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        skips = []
        h = x
        for i, conv in enumerate(self.convs):
            h = conv(torch.nn.functional.pad(h, (1, 2, 1, 2)))  # "same" for even sizes, stride 2, kernel 5
            skips.append(h)
            if i < 5:
                h = torch.nn.functional.elu(self.conv_bns[i](h))
        # conv6's BatchNorm and activation exist in the graph but feed nothing (unet.py: "_ = ...")
        for i, up in enumerate(self.ups):
            h = up(h)[:, :, 1:-2, 1:-2]
            h = self.up_bns[i](torch.nn.functional.elu(h))
            if i < 5:
                h = torch.cat([skips[4 - i], h], dim=1)
        return torch.sigmoid(self.final(h)) * x


class Spleeter(torch.nn.Module):
    """Four U-Nets and the mask formula: (est^2 + eps/4) / (sum est^2 + eps)."""

    def __init__(self):
        super().__init__()
        self.unets = torch.nn.ModuleList(UNet() for _ in STEMS)

    def estimates(self, x: torch.Tensor) -> torch.Tensor:
        return torch.stack([unet(x) for unet in self.unets], dim=1)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        est = self.estimates(x) ** EXPONENT
        return (est + EPS / len(STEMS)) / (est.sum(dim=1, keepdim=True) + EPS)


def load_weights(model: Spleeter, checkpoint: Path) -> None:
    """TF variable names count up across the four U-Nets in instrument order: 7 convs, 6 transposed convs
    and 12 batch norms per stem (the 6th batch norm is the unused one after conv6)."""
    import tensorflow as tf  # noqa: E402  heavy import, only for the reader

    reader = tf.train.load_checkpoint(str(checkpoint))

    def tensor(name: str) -> torch.Tensor:
        return torch.from_numpy(reader.get_tensor(name))

    def suffix(base: str, index: int) -> str:
        return base if index == 0 else f"{base}_{index}"

    def load_conv(layer, base: str, index: int) -> None:
        with torch.no_grad():
            layer.weight.copy_(tensor(f"{suffix(base, index)}/kernel").permute(3, 2, 0, 1))
            layer.bias.copy_(tensor(f"{suffix(base, index)}/bias"))

    def load_bn(layer, index: int) -> None:
        name = suffix("batch_normalization", index)
        with torch.no_grad():
            layer.weight.copy_(tensor(f"{name}/gamma"))
            layer.bias.copy_(tensor(f"{name}/beta"))
            layer.running_mean.copy_(tensor(f"{name}/moving_mean"))
            layer.running_var.copy_(tensor(f"{name}/moving_variance"))

    for s, unet in enumerate(model.unets):
        for i, conv in enumerate(unet.convs):
            load_conv(conv, "conv2d", 7 * s + i)
        load_conv(unet.final, "conv2d", 7 * s + 6)
        for i, bn in enumerate(unet.conv_bns):
            load_bn(bn, 12 * s + i)
        for i, up in enumerate(unet.ups):
            load_conv(up, "conv2d_transpose", 6 * s + i)
        for i, bn in enumerate(unet.up_bns):
            load_bn(bn, 12 * s + 6 + i)


def fake_magnitude(rng: np.random.Generator, splits: int, bins: int) -> np.ndarray:
    return np.abs(rng.standard_normal((splits, 2, T, bins)) * 10.0).astype(np.float32)


def export(model: Spleeter) -> Path:
    RELEASE.mkdir(parents=True, exist_ok=True)
    onnx_path = RELEASE / "spleeter-4stems.onnx"
    torch.onnx.export(
        model,
        torch.rand(1, 2, T, F_16K),
        str(onnx_path),
        input_names=["magnitude"],
        output_names=["masks"],
        dynamic_axes={"magnitude": {0: "splits", 3: "bins"}, "masks": {0: "splits", 4: "bins"}},
        opset_version=OPSET,
        dynamo=False,
    )
    return onnx_path


def parity(onnx_path: Path, model: Spleeter) -> float:
    session = onnxruntime.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    rng = np.random.default_rng(1)
    worst = 0.0
    for splits, bins in ((1, F_11K), (2, F_16K)):
        x = fake_magnitude(rng, splits, bins)
        with torch.inference_mode():
            ref = model(torch.from_numpy(x)).numpy()
        (out,) = session.run(None, {"magnitude": x})
        assert out.shape == (splits, 4, 2, T, bins), out.shape
        worst = max(worst, float(np.abs(out - ref).max()))
    return worst


def cross_check(model: Spleeter):
    """Per-stem estimates against the Best-Practice export (x [2, splits, 512, 1024] -> y, sigmoid * input)."""
    folder = OUT / "crosscheck"
    if not all((folder / f"{stem}.onnx").exists() for stem in STEMS):
        return None
    x = fake_magnitude(np.random.default_rng(2), 2, F_11K)
    with torch.inference_mode():
        ours = model.estimates(torch.from_numpy(x)).numpy()  # [splits, 4, 2, T, F]
    worst = {}
    for i, stem in enumerate(STEMS):
        session = onnxruntime.InferenceSession(str(folder / f"{stem}.onnx"), providers=["CPUExecutionProvider"])
        (theirs,) = session.run(None, {"x": x.transpose(1, 0, 2, 3)})  # [2, splits, T, F]
        worst[stem] = float(np.abs(theirs.transpose(1, 0, 2, 3) - ours[:, i]).max())
    return {"max_abs_diff_per_stem": worst, "reference_scale": float(np.abs(ours).max())}


def hann() -> np.ndarray:
    return (0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(FRAME) / FRAME)).astype(np.float64)  # periodic


def stft(channel: np.ndarray) -> np.ndarray:
    """spleeter/model/__init__.py: frame_length zeros in front, tf.signal.stft(pad_end=True), periodic Hann."""
    x = np.concatenate([np.zeros(FRAME), channel.astype(np.float64)])
    frames = int(np.ceil(len(x) / HOP))
    x = np.pad(x, (0, (frames - 1) * HOP + FRAME - len(x)))
    window = hann()
    return np.stack([np.fft.rfft(x[i * HOP:i * HOP + FRAME] * window) for i in range(frames)])  # [frames, 2049]


def istft(spec: np.ndarray, length: int) -> np.ndarray:
    """tf.signal.inverse_stft with the same window, times 2/3, the leading frame_length dropped."""
    window = hann()
    out = np.zeros((len(spec) - 1) * HOP + FRAME)
    for i, frame in enumerate(spec):
        out[i * HOP:i * HOP + FRAME] += np.fft.irfft(frame, FRAME) * window
    out *= WINDOW_COMPENSATION
    return out[FRAME:FRAME + length]


def separate(session: onnxruntime.InferenceSession, mix: np.ndarray, bins: int) -> np.ndarray:
    """mix [2, n] -> stems [4, 2, n]: the host recipe (core/separation) in numpy."""
    specs = np.stack([stft(mix[c]) for c in range(2)])  # [2, frames, 2049]
    frames = specs.shape[1]
    splits = -(-frames // T)
    magnitude = np.zeros((splits, 2, T, bins), dtype=np.float32)
    mag_full = np.abs(specs[:, :, :bins]).astype(np.float32)  # [2, frames, bins]
    for s in range(splits):
        part = mag_full[:, s * T:(s + 1) * T]
        magnitude[s, :, :part.shape[1]] = part
    (masks,) = session.run(None, {"magnitude": magnitude})  # [splits, 4, 2, T, bins]
    stems = np.zeros((4, 2, mix.shape[1]))
    for k in range(4):
        mask = np.concatenate([masks[s, k] for s in range(splits)], axis=1)[:, :frames]  # [2, frames, bins]
        full = np.zeros_like(specs)
        full[:, :, :bins] = specs[:, :, :bins] * mask  # bins above F stay zero ("zeros" extension)
        for c in range(2):
            stems[k, c] = istft(full[c], mix.shape[1])
    return stems


def fixture_mix() -> np.ndarray:
    """2 s stereo: a bass line (E1, A1), a sustained mid chord, clicks on the beat, a little noise."""
    rng = np.random.default_rng(9)
    n = SAMPLE_RATE * 2
    t = np.arange(n) / SAMPLE_RATE
    left = np.zeros(n)
    right = np.zeros(n)
    for start, hz in ((0.0, 41.2), (1.0, 55.0)):
        seg = (t >= start) & (t < start + 1.0)
        tone = np.sin(2 * np.pi * hz * t) + 0.3 * np.sin(2 * np.pi * 2 * hz * t)
        left += seg * tone * 0.4
        right += seg * tone * 0.4
    for hz in (329.6, 415.3, 493.9):
        left += 0.12 * np.sin(2 * np.pi * hz * t)
        right += 0.10 * np.sin(2 * np.pi * hz * t * 1.001)
    for beat in range(4):
        i = int(beat * 0.5 * SAMPLE_RATE)
        env = np.exp(-np.arange(2000) / 300.0)
        left[i:i + 2000] += 0.5 * env * rng.standard_normal(2000)
        right[i:i + 2000] += 0.5 * env * rng.standard_normal(2000)
    mix = np.stack([left, right]) + 0.005 * rng.standard_normal((2, n))
    return (mix / np.abs(mix).max() * 0.8).astype(np.float32)


def fixtures(onnx_path: Path) -> dict:
    session = onnxruntime.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    mix = fixture_mix()
    stems = separate(session, mix, F_16K).astype(np.float32)
    fixture_dir = APP / "tests" / "fixtures" / "separation"
    fixture_dir.mkdir(parents=True, exist_ok=True)
    mix.T.astype(np.float32).tofile(fixture_dir / "spleeter-mix.f32")  # interleaved stereo
    stems.transpose(0, 2, 1).astype(np.float32).tofile(fixture_dir / "spleeter-stems.f32")  # stem-major, interleaved
    energy = {stem: float(np.mean(stems[k] ** 2)) for k, stem in enumerate(STEMS)}
    return {"seconds": 2, "bins": F_16K, "sum_minus_mix_max": float(np.abs(stems.sum(axis=0) - mix).max()),
            "stem_energy": energy, "fixture_dir": str(fixture_dir)}


def main() -> None:
    checkpoint = download()
    model = Spleeter().eval()
    load_weights(model, checkpoint)
    onnx_path = export(model)
    worst = parity(onnx_path, model)
    print(f"{onnx_path.name}: {onnx_path.stat().st_size} bytes, onnx vs torch max abs diff {worst:.3e}")
    cross = cross_check(model)
    print(f"vs Best-Practice export: {cross}")
    fx = fixtures(onnx_path)
    print(f"fixture: {fx}")
    manifest = {
        "license": "MIT (deezer/spleeter code and pre-trained models per the JOSS paper 10.21105/joss.02154; "
                   "README names only the code, deezer/spleeter#898 unanswered; export script sbkong)",
        "release_tag": RELEASE_TAG,
        "exported": date.today().isoformat(),
        "torch": torch.__version__, "onnx_opset": OPSET,
        "source": {"url": CHECKPOINT_URL, "sha256": CHECKPOINT_SHA256},
        "stems": STEMS,
        "preprocessing": {"sample_rate": SAMPLE_RATE, "frame_length": FRAME, "frame_step": HOP, "window": "hann periodic",
                          "front_padding": FRAME, "pad_end": True, "T": T, "F": F_16K, "F_alternative": F_11K,
                          "activation": "ELU", "batchnorm_eps": 1e-3},
        "postprocessing": {"mask": "(est^2 + 1e-10/4) / (sum est^2 + 1e-10), in graph", "mask_extension": "zeros",
                           "istft": "hann periodic, times 2/3, drop the first frame_length samples"},
        "parity_max_abs_diff": worst, "cross_check": cross, "fixture": fx,
        "files": {onnx_path.name: {"size": onnx_path.stat().st_size, "sha256": sha256(onnx_path)}},
    }
    (RELEASE / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"manifest -> {RELEASE / 'manifest.json'}")


if __name__ == "__main__":
    main()
