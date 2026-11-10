# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 sbkong
"""Research-only tool: HT-Demucs -> ONNX for this machine (copy-spec.md section 4 "스템 분리 조사", path (나)).

The Demucs weights are provided by their author for research purposes only (facebookresearch/demucs issues
#327 and #508, 2026-11-10), so the app never downloads, hosts or ships this file. This script builds it on
the user's own PC from the official checkpoint (fetched by the `demucs` package from Meta's server) and puts
it where the app's separator picks it up first: <user data>/models/htdemucs/htdemucs.onnx. Public builds
use Spleeter (export_spleeter.py).

The export itself is the `demucs-onnx` package (StemSplitio, MIT): it patches the model so torch.onnx can
take it (complex STFT/iSTFT as real convolutions, a fixed positional shift, a plain attention), exports
with opset 17 and checks ONNX vs PyTorch parity (default tolerance 1e-3). This wrapper adds the second
parity check on our own random input, the mono/stereo smoke test through onnxruntime, and a manifest with
the SHA-256 the user can compare against later.

    D:\\cc_venv\\Scripts\\python.exe tools\\models\\export_htdemucs.py            # -> %APPDATA%\\CopyCoach\\CopyCoach\\models\\htdemucs\\
    D:\\cc_venv\\Scripts\\python.exe tools\\models\\export_htdemucs.py --out D:\\somewhere

Needs: pip install "demucs-onnx[export]" (torch, demucs), onnxruntime, numpy.
"""
import argparse
import hashlib
import json
import os
import sys
from datetime import date
from pathlib import Path

import numpy as np

SAMPLE_RATE = 44100
SEGMENT = 343980  # 7.8 s, the transformer's limit (core/separation/htdemucs.h)
STEMS = ["drums", "bass", "other", "vocals"]  # the model's order; the app maps it to its own
MODEL_NAME = "htdemucs"
FILE_NAME = "htdemucs.onnx"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def default_out() -> Path:
    appdata = os.environ.get("APPDATA")
    if not appdata:
        raise SystemExit("APPDATA is not set; pass --out")
    return Path(appdata) / "CopyCoach" / "CopyCoach" / "models" / "htdemucs"


def export(out_dir: Path) -> Path:
    try:
        from demucs_onnx.export import export_to_onnx  # noqa: E402
    except ImportError as e:  # pragma: no cover - environment
        raise SystemExit('demucs-onnx is not installed: pip install "demucs-onnx[export]"') from e
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / FILE_NAME
    print(f"exporting {MODEL_NAME} (Meta weights, research use only) -> {target}")
    written = export_to_onnx(MODEL_NAME, target, opset=17, parity_check=True, parity_tolerance=1e-3)
    paths = list(written.values()) if isinstance(written, dict) else [target]
    if target not in paths and paths:
        paths[0].replace(target)  # a single-file export may name the file after the model
    return target


def smoke_test(model: Path) -> dict:
    """The graph the app expects: mix [1, 2, 343980] -> stems [1, 4, 2, 343980], finite, roughly summing back."""
    import onnxruntime  # noqa: E402

    session = onnxruntime.InferenceSession(str(model), providers=["CPUExecutionProvider"])
    inputs = [i.name for i in session.get_inputs()]
    outputs = [o.name for o in session.get_outputs()]
    assert inputs == ["mix"], inputs
    assert outputs == ["stems"], outputs
    rng = np.random.default_rng(5)
    t = np.arange(SEGMENT) / SAMPLE_RATE
    mix = (0.3 * np.sin(2 * np.pi * 110 * t) + 0.05 * rng.standard_normal(SEGMENT)).astype(np.float32)
    mix = np.stack([mix, mix])[None]
    (stems,) = session.run(None, {"mix": mix})
    assert stems.shape == (1, len(STEMS), 2, SEGMENT), stems.shape
    assert np.isfinite(stems).all()
    error = float(np.mean((stems.sum(axis=1) - mix) ** 2) / np.mean(mix**2))
    return {"input": inputs, "output": outputs, "sum_minus_mix_relative_energy": error}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", type=Path, default=None, help="folder for htdemucs.onnx (default: the app's models/htdemucs)")
    args = parser.parse_args()
    out_dir = args.out or default_out()
    model = export(out_dir)
    smoke = smoke_test(model)
    print(f"smoke test: {smoke}")
    manifest = {
        "license": "weights: research use only (facebookresearch/demucs #327, #508); never distributed by the app",
        "exported": date.today().isoformat(),
        "exporter": "demucs-onnx (StemSplitio, MIT) through tools/models/export_htdemucs.py",
        "model": MODEL_NAME, "stems": STEMS, "segment": SEGMENT, "sample_rate": SAMPLE_RATE,
        "smoke_test": smoke,
        "files": {model.name: {"size": model.stat().st_size, "sha256": sha256(model)}},
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"done -> {model} ({model.stat().st_size} bytes); the app uses it before Spleeter")


if __name__ == "__main__":
    sys.exit(main())
