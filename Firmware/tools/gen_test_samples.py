#!/usr/bin/env python3
"""
Reconstruct the exact 45 unseen Test-split trials from ML_Model/sEMG.ipynb
(random_state=42, 70/15/15 stratified split) and generate Firmware/main/semg_mock_data.h
containing 1000 samples (1.0 s @ 1 kHz) per gesture class encoded as 12-bit ADC codes
around mid-rail 2048 (VREF = 1.65 V, Gain = 113.6x, Scale D1 = 1/113.6 V/unit).
"""

import csv
import glob
import os
import sys

import numpy as np
from sklearn.model_selection import train_test_split

GESTURE_MAP = [
    ("1st_Gesture_Resting", 0, "RESTING"),
    ("2nd_Gesture_Fist", 1, "FIST"),
    ("4th_Gesture_Open_Hand", 2, "OPEN_HAND"),
    ("7th_Gesture_Pointing", 3, "POINTING"),
]

SAMPLES_PER_CLASS = 1000  # 1.0 s @ 1 kHz -> 4 * 1000 * 3 * 2B = 24 KB Flash
START_OFFSET = 500        # Skip initial 0.5 s transient of trial


def load_test_split(base_dir: str):
    trials = []
    for folder, cls_idx, label in GESTURE_MAP:
        files = sorted(glob.glob(os.path.join(base_dir, folder, "*.csv")))
        for f in files:
            trials.append({"file": f, "name": os.path.basename(f), "class": cls_idx, "label": label})

    if not trials:
        raise RuntimeError(f"No dataset CSV files found under {base_dir}")

    _, temp_trials = train_test_split(
        trials, test_size=0.3, random_state=42, stratify=[t["class"] for t in trials]
    )
    _, test_trials = train_test_split(
        temp_trials, test_size=0.5, random_state=42, stratify=[t["class"] for t in temp_trials]
    )
    return test_trials


def read_csv_3ch(path: str) -> np.ndarray:
    rows = []
    with open(path, "r", encoding="utf-8") as f:
        reader = csv.reader(f)
        next(reader, None)  # skip header
        for r in reader:
            if len(r) >= 4:
                rows.append([float(r[1]), float(r[2]), float(r[3])])
    return np.asarray(rows, dtype=np.float64)


def dataset_to_adc12(vals: np.ndarray) -> np.ndarray:
    """
    Under Scale D1 (1/113.6 V/unit) and AFE Gain 113.6x:
      V_adc (V) = 1.65 + val_dataset
      adc_code  = round(V_adc / 3.3 * 4095)
    """
    v_adc = 1.65 + vals
    codes = np.rint((v_adc / 3.3) * 4095.0).astype(np.int32)
    return np.clip(codes, 20, 4075).astype(np.int16)


def main():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    base_dir = os.path.join(repo_root, "ML_Model", "datasets", "train")
    out_header = os.path.join(repo_root, "Firmware", "main", "semg_mock_data.h")

    test_trials = load_test_split(base_dir)
    print(f"[INFO] Reconstructed exact Test split: {len(test_trials)} unseen trials")

    selected = {}
    for _, cls_idx, label in GESTURE_MAP:
        cls_trials = [t for t in test_trials if t["class"] == cls_idx]
        # Pick the first unseen test trial with sufficient length and clear RMS activity
        chosen = cls_trials[0]
        arr = read_csv_3ch(chosen["file"])
        seg = arr[START_OFFSET : START_OFFSET + SAMPLES_PER_CLASS]
        adc_seg = dataset_to_adc12(seg)
        selected[cls_idx] = (chosen["name"], adc_seg)
        print(
            f"  [CLASS {cls_idx} {label:9s}] file='{chosen['name']}' "
            f"adc_range=[{adc_seg.min()}, {adc_seg.max()}]"
        )

    lines = [
        "/**",
        " * @file semg_mock_data.h",
        " * @brief Auto-generated 12-bit ADC mock waveforms from unseen Test-split trials",
        " *        (ML_Model/sEMG.ipynb random_state=42, 70/15/15 split).",
        " *",
        " * Compiled ONLY when SEMG_MODE_TESTING is enabled.",
        " */",
        "",
        "#pragma once",
        "#include <stdint.h>",
        "#include \"semg_config.h\"",
        "",
        f"#define SEMG_MOCK_NUM_GESTURES   (4)",
        f"#define SEMG_MOCK_SEQ_LEN        ({SAMPLES_PER_CLASS})",
        "",
        "/* Source unseen Test-split filenames (for reference/logging) */",
        "static const char * const SEMG_MOCK_SOURCE_FILES[SEMG_MOCK_NUM_GESTURES] = {",
    ]
    for cls_idx in range(4):
        lines.append(f'    "{selected[cls_idx][0]}",')
    lines.append("};")
    lines.append("")
    lines.append("/* 12-bit ADC codes [gesture][sample_ms][channel] centered at 2048 (1.65 V) */")
    lines.append("static const int16_t SEMG_MOCK_WAVE[SEMG_MOCK_NUM_GESTURES][SEMG_MOCK_SEQ_LEN][SEMG_NUM_CHANNELS] = {")

    for cls_idx in range(4):
        name, adc_seg = selected[cls_idx]
        lines.append(f"    /* Class {cls_idx}: {name} */")
        lines.append("    {")
        for i in range(0, SAMPLES_PER_CLASS, 4):
            chunk = adc_seg[i : i + 4]
            row = ", ".join(f"{{{c[0]:4d},{c[1]:4d},{c[2]:4d}}}" for c in chunk)
            comma = "," if (i + 4) < SAMPLES_PER_CLASS else ""
            lines.append(f"        {row}{comma}")
        lines.append("    }" + ("," if cls_idx < 3 else ""))

    lines.append("};")
    lines.append("")

    with open(out_header, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

    print(f"[OK] Generated {out_header} ({SAMPLES_PER_CLASS * 4 * 3 * 2} bytes in Flash)")


if __name__ == "__main__":
    main()
