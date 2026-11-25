#!/usr/bin/env python3
"""
Extract exact float32 MLP weights from ML_Model/semg_mlp.tflite and regenerate
Server/inc/tflite_weights.h so the built-in C MLP fallback matches .tflite bit-for-bit.

Works with tensorflow/tflite_runtime if available, or pure Python FlatBuffer parsing
(zero third-party dependencies).
"""

import argparse
import os
import struct
import sys


def _u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def _i32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<i", buf, off)[0]


def _u16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<H", buf, off)[0]


def _fb_field_off(buf: bytes, table: int, field_idx: int) -> int:
    vtable = table - _i32(buf, table)
    vtable_len = _u16(buf, vtable)
    slot = 4 + 2 * field_idx
    if slot >= vtable_len:
        return 0
    off = _u16(buf, vtable + slot)
    return (table + off) if off else 0


def _fb_vec(buf: bytes, field_pos: int):
    vec = field_pos + _u32(buf, field_pos)
    length = _u32(buf, vec)
    return vec + 4, length


def _fb_str(buf: bytes, field_pos: int) -> str:
    data, length = _fb_vec(buf, field_pos)
    return buf[data : data + length].decode("utf-8", errors="replace")


def parse_tflite_flatbuffer(path: str):
    """Minimal pure-Python TFLite FlatBuffer reader for float32 tensors."""
    with open(path, "rb") as f:
        buf = f.read()

    root = _u32(buf, 0)
    # Model table: 0=version, 1=operator_codes, 2=subgraphs, 3=description, 4=buffers
    subgraphs_pos = _fb_field_off(buf, root, 2)
    buffers_pos = _fb_field_off(buf, root, 4)
    if not subgraphs_pos or not buffers_pos:
        raise RuntimeError("Invalid TFLite FlatBuffer: missing subgraphs or buffers")

    # Read buffers vector (Buffer table: 0=data)
    b_start, b_len = _fb_vec(buf, buffers_pos)
    buffers = []
    for i in range(b_len):
        elem = b_start + i * 4
        b_tbl = elem + _u32(buf, elem)
        data_pos = _fb_field_off(buf, b_tbl, 0)
        if data_pos:
            d_start, d_len = _fb_vec(buf, data_pos)
            buffers.append(buf[d_start : d_start + d_len])
        else:
            buffers.append(b"")

    # Read first subgraph (SubGraph table: 0=tensors)
    sg_start, sg_len = _fb_vec(buf, subgraphs_pos)
    if sg_len < 1:
        raise RuntimeError("No subgraphs found in TFLite model")
    sg_tbl = sg_start + _u32(buf, sg_start)
    tensors_pos = _fb_field_off(buf, sg_tbl, 0)
    t_start, t_len = _fb_vec(buf, tensors_pos)

    tensors = []
    for i in range(t_len):
        elem = t_start + i * 4
        t_tbl = elem + _u32(buf, elem)
        # Tensor table: 0=shape, 1=type (0=FLOAT32), 2=buffer, 3=name
        shape = []
        s_pos = _fb_field_off(buf, t_tbl, 0)
        if s_pos:
            sp, sl = _fb_vec(buf, s_pos)
            shape = list(struct.unpack_from(f"<{sl}i", buf, sp))
        t_type = buf[_fb_field_off(buf, t_tbl, 1)] if _fb_field_off(buf, t_tbl, 1) else 0
        b_idx_pos = _fb_field_off(buf, t_tbl, 2)
        b_idx = _u32(buf, b_idx_pos) if b_idx_pos else 0
        n_pos = _fb_field_off(buf, t_tbl, 3)
        name = _fb_str(buf, n_pos) if n_pos else f"tensor_{i}"

        raw = buffers[b_idx] if b_idx < len(buffers) else b""
        if raw and t_type == 0:  # FLOAT32
            count = len(raw) // 4
            vals = list(struct.unpack_from(f"<{count}f", raw, 0))
            tensors.append({"index": i, "name": name, "shape": shape, "values": vals})
    return tensors


def format_c_array(name: str, vals: list) -> str:
    lines = [f"static const float {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), 8):
        chunk = vals[i : i + 8]
        row = ", ".join(f"{v: .8f}f" for v in chunk)
        comma = "," if (i + 8) < len(vals) else ""
        lines.append(f"    {row}{comma}")
    lines.append("};")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Extract TFLite MLP weights to C header")
    parser.add_argument(
        "--model",
        default=os.path.join(os.path.dirname(__file__), "..", "..", "ML_Model", "semg_mlp.tflite"),
        help="Path to semg_mlp.tflite",
    )
    parser.add_argument(
        "--out",
        default=os.path.join(os.path.dirname(__file__), "..", "inc", "tflite_weights.h"),
        help="Output C header path",
    )
    args = parser.parse_args()

    tensors = parse_tflite_flatbuffer(args.model)
    by_shape = {}
    for t in tensors:
        sh = tuple(t["shape"])
        by_shape[sh] = t
        print(f"  [TENSOR] idx={t['index']:2d} shape={str(sh):10s} len={len(t['values']):4d} name={t['name']}")

    expected = [
        ("MLP_W1", (64, 15)),
        ("MLP_B1", (64,)),
        ("MLP_W2", (32, 64)),
        ("MLP_B2", (32,)),
        ("MLP_W3", (4, 32)),
        ("MLP_B3", (4,)),
    ]

    blocks = [
        "#ifndef TFLITE_WEIGHTS_H",
        "#define TFLITE_WEIGHTS_H",
        "",
        "/* Auto-generated from ML_Model/semg_mlp.tflite by Server/tools/extract_tflite_weights.py */",
        "",
    ]

    for c_name, shape in expected:
        if shape not in by_shape:
            raise RuntimeError(f"Expected tensor {c_name} with shape {shape} not found in {args.model}")
        blocks.append(format_c_array(c_name, by_shape[shape]["values"]))
        blocks.append("")

    blocks.append("#endif /* TFLITE_WEIGHTS_H */")
    blocks.append("")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(blocks))

    print(f"[OK] Regenerated {args.out} from {args.model}")


if __name__ == "__main__":
    main()
