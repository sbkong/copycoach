# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 sbkong
"""Research-only tool: BS-RoFormer-SW (6 stems) -> ONNX for this machine (copy-spec.md section 4 "스템 모델 재조사
2026-11-17", user decision A).

The checkpoint's provenance is unknown (nobody has claimed training it; ZFTurbo/Music-Source-Separation-Training
issue #248), so the app never downloads, hosts or ships the result. This script builds it on the user's own PC
and puts it where the app's separator picks it up first: <user data>/models/bs_roformer/bs_roformer.onnx.

Graph: the model without its STFT (ZFTurbo/MSS_ONNX_TensorRT models_without_stft/bs_roformer_no_stft.py, MIT),
input `spec [1, T, 4100]` = the complex STFT (n_fft 2048, hop 512, periodic Hann, centred with reflect padding)
of one 588800-sample chunk (13.35 s, T = 1151 frames), last axis ordered (bin, channel, re/im); output
`masks [1, 6, 2050, T, 2]` = complex masks per stem (bass, drums, other, vocals, guitar, piano) over the
(bin, channel) axis. The host multiplies, inverts the STFT and overlap-adds chunks the way MSST's demix does
(step chunk/2, linear fades of chunk/10). T is fixed: the rotary embedding bakes the sequence length in.

    D:\\cc_venv\\Scripts\\python.exe tools\\models\\export_bs_roformer.py           # -> %APPDATA%\\CopyCoach\\CopyCoach\\models\\bs_roformer\\
    D:\\cc_venv\\Scripts\\python.exe tools\\models\\export_bs_roformer.py --out D:\\somewhere

Needs: torch, onnx, onnxruntime, einops, rotary-embedding-torch, pyyaml, numpy (D:\\cc_venv has them).
Downloads (into build/_downloads/bs_roformer/): the two MIT model files from ZFTurbo and the checkpoint + yaml
from the Hugging Face mirror, all SHA-256 checked.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import types
import urllib.request
from datetime import date
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DOWNLOADS = ROOT / "build" / "_downloads" / "bs_roformer"
SAMPLE_RATE = 44100
CHUNK = 588800  # config audio.chunk_size: 13.35 s
N_FFT = 2048
HOP = 512
FRAMES = 1 + CHUNK // HOP  # torch.stft(center=True): 1151
BINS = N_FFT // 2 + 1
STEMS = ["bass", "drums", "other", "vocals", "guitar", "piano"]  # training.instruments; the app maps the order
FILE_NAME = "bs_roformer.onnx"

FILES = {
    "src/models/bs_roformer/bs_roformer_no_stft.py": (
        "https://raw.githubusercontent.com/ZFTurbo/MSS_ONNX_TensorRT/main/models_without_stft/bs_roformer_no_stft.py",
        None),
    "src/models/bs_roformer/attend.py": (
        "https://raw.githubusercontent.com/ZFTurbo/Music-Source-Separation-Training/main/models/bs_roformer/attend.py",
        None),
    "BS-Rofo-SW-Fixed.yaml": (
        "https://huggingface.co/enerjazzer/BS-ROFO-SW-Fixed/resolve/main/BS-Rofo-SW-Fixed.yaml", None),
    "BS-Rofo-SW-Fixed.ckpt": (
        "https://huggingface.co/enerjazzer/BS-ROFO-SW-Fixed/resolve/main/BS-Rofo-SW-Fixed.ckpt",
        "24e7d35ee9c64415673d3fd33e06a67cac2c103c5df6267ba1576459c775916e"),
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def fetch_all() -> dict:
    """Downloads what is missing; returns {relative name: sha256} of everything used."""
    hashes = {}
    for rel, (url, expected) in FILES.items():
        path = DOWNLOADS / rel
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            print(f"downloading {url}")
            urllib.request.urlretrieve(url, path)
        digest = sha256(path)
        if expected and digest != expected:
            raise SystemExit(f"{rel}: SHA-256 {digest} != expected {expected}")
        hashes[rel] = digest
    return hashes


ATTENTION_BYTES = 512 << 20  # a WebGPU storage buffer is at most 2 GB; the full time-attention map is 2.6 GB


def chunk_attention(attend_class) -> None:
    """The time transformer attends over 1151 frames for 62 bands x 8 heads at once: its score map
    (62 x 8 x 1151 x 1151 floats, 2.6 GB) is too large for one WebGPU buffer, and ORT's provider then
    returns garbage without an error (seen 2026-11-17). The export computes attention in batch pieces
    whose maps stay under ATTENTION_BYTES; the numbers are identical, only the graph is sliced."""
    import torch  # noqa: E402

    original = attend_class.forward

    def forward(self, q, k, v):
        batch, heads, n, _ = q.shape
        per_item = heads * n * n * 4
        step = max(1, min(batch, ATTENTION_BYTES // per_item))
        if step >= batch:
            return original(self, q, k, v)
        return torch.cat([original(self, q[i:i + step], k[i:i + step], v[i:i + step]) for i in range(0, batch, step)], 0)

    attend_class.forward = forward


def load_model():
    """The no-STFT model with the checkpoint's weights, attention as plain matmuls (flash_attn off)."""
    import torch  # noqa: E402
    import yaml  # noqa: E402

    # beartype is only decoration in that module; a stub keeps the venv small.
    stub = types.ModuleType("beartype")
    stub.beartype = lambda fn: fn
    stub.typing = types.SimpleNamespace(**{n: getattr(__import__("typing"), n) for n in ("Tuple", "Optional", "List", "Callable")})
    sys.modules.setdefault("beartype", stub)
    sys.modules.setdefault("beartype.typing", stub.typing)
    src = DOWNLOADS / "src"
    (src / "models" / "__init__.py").touch()
    (src / "models" / "bs_roformer" / "__init__.py").touch()
    sys.path.insert(0, str(src))
    from models.bs_roformer.bs_roformer_no_stft import BSRoformer  # noqa: E402
    from models.bs_roformer.attend import Attend  # noqa: E402

    chunk_attention(Attend)

    with open(DOWNLOADS / "BS-Rofo-SW-Fixed.yaml", encoding="utf-8") as f:
        config = yaml.load(f, Loader=yaml.FullLoader)
    kwargs = dict(config["model"])
    kwargs["flash_attn"] = False
    kwargs["freqs_per_bands"] = tuple(kwargs["freqs_per_bands"])
    kwargs["multi_stft_resolutions_window_sizes"] = tuple(kwargs["multi_stft_resolutions_window_sizes"])
    assert kwargs["num_stems"] == len(STEMS) and config["training"]["instruments"] == STEMS
    assert config["audio"]["chunk_size"] == CHUNK and kwargs["stft_hop_length"] == HOP and kwargs["stft_n_fft"] == N_FFT
    model = BSRoformer(**kwargs)
    state = torch.load(DOWNLOADS / "BS-Rofo-SW-Fixed.ckpt", map_location="cpu", weights_only=True)
    if "state" in state:
        state = state["state"]
    if "state_dict" in state:
        state = state["state_dict"]
    missing, unexpected = model.load_state_dict(state, strict=False)
    if missing or unexpected:
        raise SystemExit(f"checkpoint mismatch: missing {missing[:5]} unexpected {unexpected[:5]}")
    return model.eval()


FAN_LIMIT = 8  # WebGPU shaders get 16 storage buffers; the 62-band Split/Concat must become trees


class FanTrees:
    """Rewrites every Split with more outputs, and every Concat with more inputs, than FAN_LIMIT into a tree
    of narrower ones. ORT's WebGPU provider refuses a 62-way Split ("Too many storage buffers in shader");
    the result is the same tensors, just in more steps."""

    def __init__(self, graph):
        import onnx  # noqa: E402

        self.onnx = onnx
        self.graph = graph
        self.initializers = {i.name: i for i in graph.initializer}
        for node in graph.node: # a Split's sizes may come from a Constant node instead
            if node.op_type == "Constant":
                self.initializers[node.output[0]] = next(a.t for a in node.attribute if a.name == "value")
        self.counter = 0
        self.nodes = []

    def name(self) -> str:
        self.counter += 1
        return f"fan_tree_{self.counter}"

    def rewrite(self) -> int:
        from onnx import helper  # noqa: E402

        wide = 0
        for node in self.graph.node:
            axis = next((a.i for a in node.attribute if a.name == "axis"), 0)
            if node.op_type == "Split" and len(node.output) > FAN_LIMIT:
                sizes = self.onnx.numpy_helper.to_array(self.initializers[node.input[1]]).tolist()
                self.split_tree(node.input[0], list(node.output), sizes, axis)
                wide += 1
            elif node.op_type == "Concat" and len(node.input) > FAN_LIMIT:
                self.concat_tree(list(node.input), node.output[0], axis)
                wide += 1
            else:
                self.nodes.append(node)
        del self.graph.node[:]
        self.graph.node.extend(self.nodes)
        return wide

    @staticmethod
    def groups(items: list) -> list:
        per = -(-len(items) // FAN_LIMIT)
        return [items[i:i + per] for i in range(0, len(items), per)]

    def split_tree(self, data: str, outputs: list, sizes: list, axis: int) -> None:
        from onnx import helper, numpy_helper  # noqa: E402

        if len(outputs) <= FAN_LIMIT:
            split_name = self.name()
            self.graph.initializer.append(numpy_helper.from_array(np.asarray(sizes, dtype=np.int64), split_name))
            self.nodes.append(helper.make_node("Split", [data, split_name], outputs, axis=axis))
            return
        members = self.groups(list(zip(outputs, sizes)))
        group_outputs = [g[0][0] if len(g) == 1 else self.name() for g in members]
        split_name = self.name()
        self.graph.initializer.append(
            numpy_helper.from_array(np.asarray([sum(s for _, s in g) for g in members], dtype=np.int64), split_name))
        self.nodes.append(helper.make_node("Split", [data, split_name], group_outputs, axis=axis))
        for out, g in zip(group_outputs, members):
            if len(g) > 1:
                self.split_tree(out, [o for o, _ in g], [s for _, s in g], axis)

    def concat_tree(self, inputs: list, output: str, axis: int) -> None:
        from onnx import helper  # noqa: E402

        if len(inputs) <= FAN_LIMIT:
            self.nodes.append(helper.make_node("Concat", inputs, [output], axis=axis))
            return
        intermediates = []
        for g in self.groups(inputs):
            if len(g) == 1:
                intermediates.append(g[0])
            else:
                name = self.name()
                self.nodes.append(helper.make_node("Concat", g, [name], axis=axis))
                intermediates.append(name)
        self.concat_tree(intermediates, output, axis)


def trace(target: Path) -> None:
    """Stage 1, its own process (the trace holds the weights twice and 2.6 GB attention maps per layer): the
    model to ONNX opset 17 with the fixed chunk, plus torch's masks for a seeded random spectrum."""
    import torch  # noqa: E402

    model = load_model()
    torch.manual_seed(5)
    spec = torch.randn(1, FRAMES, 2 * 2 * BINS) * 0.1
    with torch.no_grad():
        np.save(target.with_suffix(".trace-reference.npy"), model(spec).numpy())
    torch.onnx.export(model, spec, str(target), opset_version=17, input_names=["spec"], output_names=["masks"],
                      dynamo=False, do_constant_folding=True)


def positive_axes(model) -> int:
    """Negative `axis`/`axes` attributes become positive (ORT's WebGPU provider has been seen to mishandle them;
    the reference export for the browser did the same). Ranks come from shape inference."""
    import onnx  # noqa: E402

    inferred = onnx.shape_inference.infer_shapes(model)
    ranks = {}
    for info in list(inferred.graph.value_info) + list(inferred.graph.input) + list(inferred.graph.output):
        ranks[info.name] = len(info.type.tensor_type.shape.dim)
    for init in model.graph.initializer:
        ranks[init.name] = len(init.dims)
    changed = 0
    for node in model.graph.node:
        rank = ranks.get(node.input[0]) if node.input else None
        for attr in node.attribute:
            if rank is None:
                continue
            if attr.name == "axis" and attr.i < 0:
                attr.i += rank
                changed += 1
            elif attr.name == "axes" and any(a < 0 for a in attr.ints):
                values = [a + rank if a < 0 else a for a in attr.ints]
                del attr.ints[:]
                attr.ints.extend(values)
                changed += 1
    return changed


def rewrite_and_check(target: Path) -> float:
    """Stage 2: wide Split/Concat as trees, then the max |ORT - torch| on the traced spectrum."""
    import onnx  # noqa: E402
    import onnxruntime  # noqa: E402
    import torch  # noqa: E402

    graph = onnx.load(str(target))
    wide = FanTrees(graph.graph).rewrite()
    print(f"rewrote {wide} wide Split/Concat nodes into trees of at most {FAN_LIMIT}")
    print(f"made {positive_axes(graph)} negative axis attributes positive")
    onnx.save(graph, str(target))
    del graph
    onnx.checker.check_model(str(target))
    torch.manual_seed(5)
    spec = torch.randn(1, FRAMES, 2 * 2 * BINS) * 0.1
    reference = np.load(target.with_suffix(".trace-reference.npy"))
    session = onnxruntime.InferenceSession(str(target), providers=["CPUExecutionProvider"])
    (masks,) = session.run(None, {"spec": spec.numpy()})
    assert masks.shape == (1, len(STEMS), 2 * BINS, FRAMES, 2), masks.shape
    target.with_suffix(".trace-reference.npy").unlink()
    return float(np.abs(masks - reference).max())


def separate_reference(session, interleaved: np.ndarray) -> np.ndarray:
    """MSST demix for one chunk-sized song (the host algorithm in Python): stems [6, 2, n] for a short mix."""
    import torch  # noqa: E402

    n = interleaved.shape[0] // 2
    audio = torch.from_numpy(interleaved.reshape(n, 2).T.copy())  # [2, n]
    assert n <= CHUNK, "the reference covers one chunk"
    padded = torch.nn.functional.pad(audio, (0, CHUNK - n))  # the demix pads a short song with zeros
    window = torch.hann_window(N_FFT)
    stft = torch.stft(padded, N_FFT, HOP, N_FFT, window=window, center=True, return_complex=True)  # [2, f, t]
    spec = torch.view_as_real(stft)  # [2, f, t, 2]
    x = spec.permute(2, 1, 0, 3).reshape(1, FRAMES, 2 * 2 * BINS)  # b t (f s c)
    (masks,) = session.run(None, {"spec": x.numpy()})
    masks = torch.from_numpy(masks).reshape(1, len(STEMS), BINS, 2, FRAMES, 2)  # b n (f s) t c
    masked = torch.view_as_complex(masks[0].permute(0, 2, 1, 3, 4).contiguous()) * stft.unsqueeze(0)  # [n, s, f, t]
    out = torch.istft(masked.reshape(-1, BINS, FRAMES), N_FFT, HOP, N_FFT, window=window, center=True, length=CHUNK)
    return out.reshape(len(STEMS), 2, CHUNK)[:, :, :n].numpy()


def default_out() -> Path:
    appdata = os.environ.get("APPDATA")
    if not appdata:
        raise SystemExit("APPDATA is not set; pass --out")
    return Path(appdata) / "CopyCoach" / "CopyCoach" / "models" / "bs_roformer"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--out", type=Path, default=None, help="folder for bs_roformer.onnx (default: the app's models/bs_roformer)")
    parser.add_argument("--stage", choices=["all", "trace", "rewrite"], default="all",
                        help="internal: 'trace' runs stage 1 alone, 'rewrite' stage 2 on an existing trace")
    args = parser.parse_args()
    out_dir = args.out or default_out()
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / FILE_NAME
    if args.stage == "trace":
        trace(target)
        return
    hashes = fetch_all()
    print(f"exporting BS-RoFormer-SW (provenance unknown, research use only) -> {target}")
    if args.stage != "rewrite":
        subprocess.run([sys.executable, "-I", __file__, "--out", str(out_dir), "--stage", "trace"], check=True)
    parity = rewrite_and_check(target)
    print(f"ORT vs torch max difference: {parity:.3e}")
    import onnxruntime  # noqa: E402

    session = onnxruntime.InferenceSession(str(target), providers=["CPUExecutionProvider"])
    mix = np.fromfile(ROOT / "tests" / "fixtures" / "separation" / "spleeter-mix.f32", dtype=np.float32)
    stems = separate_reference(session, mix)
    stems.astype(np.float32).tofile(out_dir / "reference-stems.f32")  # [6, 2, n] planar, for the C++ test
    total = stems.sum(axis=0).T.reshape(-1)
    residual = float(np.mean((total - mix) ** 2) / np.mean(mix**2))
    print(f"fixture: stems sum minus mix, relative energy {residual:.3e}")
    manifest = {
        "license": "weights: provenance unknown, no license (MSST issue #248); never distributed by the app. Model code MIT (ZFTurbo).",
        "exported": date.today().isoformat(),
        "model": "BS-RoFormer-SW 6 stems", "stems": STEMS, "chunk": CHUNK, "frames": FRAMES, "n_fft": N_FFT, "hop": HOP,
        "sample_rate": SAMPLE_RATE, "opset": 17, "fan_limit": FAN_LIMIT, "attention_bytes": ATTENTION_BYTES, "ort_vs_torch_max_abs": parity, "fixture_sum_residual": residual,
        "inputs": hashes,
        "files": {FILE_NAME: {"size": target.stat().st_size, "sha256": sha256(target)}},
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"done -> {target} ({target.stat().st_size} bytes); the app uses it before htdemucs and Spleeter")


if __name__ == "__main__":
    sys.exit(main())
