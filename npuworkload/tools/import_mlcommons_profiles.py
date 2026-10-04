#!/usr/bin/env python3
"""Import pinned MLCommons Tiny models and deterministic model-ready inputs."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import math
import re
import shutil
import struct
import zipfile
from pathlib import Path


PINNED_COMMIT = "4addd0fa08d216e20637637874e084895f289da4"

SOURCES = {
    "kws_model": (
        "benchmark/training/keyword_spotting/trained_models/kws_ref_model.tflite",
        "aeea436800704fce17b17292e4412630ad856e9d777c044c64ef748a880bd0ae",
    ),
    "ic_model": (
        "benchmark/training/image_classification/trained_models/pretrainedResnet_quant.tflite",
        "3c002613d1b2475eb51dd78dfb85a546c8ae658dee71cf6ade43b022fe205415",
    ),
    "ad_model": (
        "benchmark/training/anomaly_detection/trained_models/ad01_int8.tflite",
        "87cf24194ef93d1d9b11a591d805526b98008e351655d29883c825c9c106ba24",
    ),
    "sww_model": (
        "benchmark/training/streaming_wakeword/trained_models/str_ww_ref_model.tflite",
        "3af8550895ba7d5c584277102b5075c52dcfa63ba9d2b2240f37c4e6abd5dd2b",
    ),
    "kws_input": (
        "benchmark/reference_submissions/keyword_spotting/kws/kws_input_data.cc",
        "a55dba6326bb57c11e3fdac2d96e13a2c36159c9fd58ac9877ef343d83cfdd58",
    ),
    "ic_input": (
        "benchmark/reference_submissions/image_classification/ic/ic_inputs.cc",
        "e3fd393d8709fb174106e6527191bc26ffebdaaac5294c9f7f862e1db1035fd8",
    ),
    "ad_input": (
        "benchmark/reference_submissions/anomaly_detection/datasets/dcase01/normal_id_01_00000000_hist_librosa.bin",
        "31bc130d27e3732e1c09db946ccc7bfa130f98739bcd90cfa39d590f61f4d6fa",
    ),
    "sww_input": (
        "benchmark/training/streaming_wakeword/calibration_samples.npz",
        "43e84d3ac605461d9b08d14d927a8c562d9c09e4e02b8ef6d89bba2be16c6533",
    ),
    "license": (
        "LICENSE.md",
        "eb3d7b5485466acbd81f2b496f595ab637d2792e268206b27d99e793bdb67549",
    ),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source(repo: Path, key: str) -> Path:
    relative, expected = SOURCES[key]
    path = repo / relative
    if not path.is_file():
        raise RuntimeError(f"missing MLCommons source: {relative}")
    actual = sha256(path)
    if actual != expected:
        raise RuntimeError(f"source hash mismatch for {relative}: {actual}")
    return path


def c_array_values(path: Path) -> list[int]:
    text = path.read_text(encoding="utf-8")
    initializer = text.split("=", 1)[1]
    initializer = re.sub(r"//.*?$|/\*.*?\*/", "", initializer,
                         flags=re.MULTILINE | re.DOTALL)
    return [int(value) for value in re.findall(r"(?<![A-Za-z_])-?\d+", initializer)]


def npy_values_from_npz(path: Path, member: str) -> tuple[tuple[int, ...], list[float]]:
    with zipfile.ZipFile(path) as archive:
        data = archive.read(member)
    if not data.startswith(b"\x93NUMPY"):
        raise RuntimeError(f"{member} is not an NPY member")
    major = data[6]
    if major == 1:
        header_length = struct.unpack_from("<H", data, 8)[0]
        header_offset = 10
    elif major in (2, 3):
        header_length = struct.unpack_from("<I", data, 8)[0]
        header_offset = 12
    else:
        raise RuntimeError(f"unsupported NPY version {major}")
    header = ast.literal_eval(data[header_offset:header_offset + header_length].decode("latin1"))
    if header["descr"] != "<f8" or header["fortran_order"]:
        raise RuntimeError(f"unsupported calibration array encoding: {header}")
    shape = tuple(int(dimension) for dimension in header["shape"])
    count = math.prod(shape)
    values = list(struct.unpack_from(f"<{count}d", data, header_offset + header_length))
    return shape, values


def quantize(values: list[float], scale: float, zero_point: int) -> bytes:
    quantized = []
    for value in values:
        converted = int(round(value / scale)) + zero_point
        quantized.append(max(-128, min(127, converted)) & 0xFF)
    return bytes(quantized)


def write_asset(output: Path, profile: str, model: Path, input_data: bytes) -> dict[str, str]:
    asset_dir = output / "profiles" / profile / "assets"
    asset_dir.mkdir(parents=True, exist_ok=True)
    model_output = asset_dir / "model.tflite"
    input_output = asset_dir / "input.bin"
    shutil.copyfile(model, model_output)
    input_output.write_bytes(input_data)
    return {"model": sha256(model_output), "input": sha256(input_output)}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mlcommons_root", type=Path)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    repo = args.mlcommons_root.resolve()
    output = args.output.resolve()

    kws = c_array_values(source(repo, "kws_input"))
    if len(kws) != 490 or any(value < -128 or value > 127 for value in kws):
        raise RuntimeError("unexpected KWS reference input")

    ic = c_array_values(source(repo, "ic_input"))
    if len(ic) != 3072 or any(value < 0 or value > 255 for value in ic):
        raise RuntimeError("unexpected IC reference input")

    ad_bytes = source(repo, "ad_input").read_bytes()
    ad = list(struct.unpack_from("<640f", ad_bytes))

    sww_shape, sww = npy_values_from_npz(source(repo, "sww_input"), "specs.npy")
    if sww_shape != (45, 30, 1, 40):
        raise RuntimeError(f"unexpected SWW calibration shape: {sww_shape}")

    results = {
        "kws01": write_asset(output, "kws01", source(repo, "kws_model"),
                              bytes(value & 0xFF for value in kws)),
        "ic01": write_asset(output, "ic01", source(repo, "ic_model"),
                             bytes((value - 128) & 0xFF for value in ic)),
        "ad01": write_asset(output, "ad01", source(repo, "ad_model"),
                             quantize(ad, 0.3910152316093445, 89)),
        "sww01": write_asset(output, "sww01", source(repo, "sww_model"),
                              quantize(sww[:1200], 0.003701042616739869, -128)),
    }

    license_dir = output / "third_party" / "mlcommons-tiny"
    license_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source(repo, "license"), license_dir / "LICENSE.md")
    provenance = {
        "upstream": "https://github.com/mlcommons/tiny",
        "commit": PINNED_COMMIT,
        "license": "Apache-2.0",
        "assets": results,
    }
    (license_dir / "PROVENANCE.json").write_text(
        json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(provenance, indent=2))


if __name__ == "__main__":
    main()
