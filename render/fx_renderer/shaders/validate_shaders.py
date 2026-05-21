#!/usr/bin/env python3
"""Validate GLSL shaders via temp-file preprocessing.

Direct validation fails:

- %d placeholders
- concatenation of dependency files (corner_alpha, gradient)
- missing precision qualifiers when compiled standalone

The script patches these up in a temp directory, runs glslangValidator, and cleans up.

Use --keep-temp to inspect the generated files.
"""

import os
import sys
import subprocess
import tempfile
import shutil

SHADER_DIR = os.path.dirname(os.path.abspath(__file__))

CONFIGS = [
    # no preprocessing needed
    ("common.vert", "common.vert", "vert", [], []),
    ("blur1", "blur1.frag", "frag", [], []),
    ("blur2", "blur2.frag", "frag", [], []),
    ("blur_effects", "blur_effects.frag", "frag", [], []),

    # gradient.frag references LEN
    # which is defined by the includer shader (quad_grad, quad_grad_round).
    ("corner_alpha", "corner_alpha.frag", "frag", [], []),

    # quad.frag: EFFECTS=(0|1), +corner_alpha when EFFECTS=1
    ("quad (effects=0)", "quad.frag", "frag", [0], []),
    ("quad (effects=1)", "quad.frag", "frag", [1], ["corner_alpha.frag"]),

    # quad_grad.frag: LEN=%d, +gradient
    ("quad_grad", "quad_grad.frag", "frag", [10], ["gradient.frag"]),

    # quad_round.frag: +corner_alpha
    ("quad_round", "quad_round.frag", "frag", [], ["corner_alpha.frag"]),

    # quad_grad_round.frag: LEN=%d, +gradient, +corner_alpha
    ("quad_grad_round", "quad_grad_round.frag", "frag", [10],
     ["gradient.frag", "corner_alpha.frag"]),

    # box_shadow.frag: +corner_alpha
    ("box_shadow", "box_shadow.frag", "frag", [], ["corner_alpha.frag"]),

    # tex.frag: SOURCE=(1|2|3) x EFFECTS=(0|1)
    ("tex (src=1,eff=0)", "tex.frag", "frag", [1, 0], []),
    ("tex (src=2,eff=0)", "tex.frag", "frag", [2, 0], []),
    ("tex (src=3,eff=0)", "tex.frag", "frag", [3, 0], []),
    ("tex (src=1,eff=1)", "tex.frag", "frag", [1, 1], ["corner_alpha.frag"]),
    ("tex (src=2,eff=1)", "tex.frag", "frag", [2, 1], ["corner_alpha.frag"]),
    ("tex (src=3,eff=1)", "tex.frag", "frag", [3, 1], ["corner_alpha.frag"]),
]

NEEDS_PRECISION = {"corner_alpha.frag"}
PRECISION_PREAMBLE = "precision mediump float;\n"


def preprocess(config, tmpdir):
    name, src_file, stage, substs, concats = config

    path = os.path.join(SHADER_DIR, src_file)
    with open(path, encoding="utf-8") as f:
        content = f.read()

    for _val in substs:
        content = content.replace("%d", str(_val), 1)

    if src_file in NEEDS_PRECISION:
        content = PRECISION_PREAMBLE + content

    for dep_file in concats:
        dep_path = os.path.join(SHADER_DIR, dep_file)
        with open(dep_path, encoding="utf-8") as f:
            content += "\n" + f.read()

    safe_name = name.translate(
        {ord(c): "_" for c in " ()=,"}
    ).replace("__", "_")
    tmpfile = os.path.join(tmpdir, f"{safe_name}.{stage}")
    with open(tmpfile, "w", encoding="utf-8") as f:
        f.write(content)

    return tmpfile


def validate_shaders():
    keep = "--keep-temp" in sys.argv
    tmpdir = tempfile.mkdtemp(prefix="shader_validate_")
    errors = 0

    try:
        for config in CONFIGS:
            name = config[0]
            stage = config[2]
            tmpfile = preprocess(config, tmpdir)

            print(f"Validating {name}...")
            result = subprocess.run(
                ["glslangValidator", "-S", stage, tmpfile],
                capture_output=True, text=True,
            )

            if result.returncode != 0:
                errors += 1
                sys.stderr.write(result.stdout)
                sys.stderr.write(result.stderr)
                sys.stderr.flush()
            elif result.stdout.strip():
                print(result.stdout, end='')
    finally:
        if keep:
            print(f"Temp files kept at: {tmpdir}")
        else:
            shutil.rmtree(tmpdir)

    if errors > 0:
        print(f"\n{errors} shader configuration(s) failed validation")
        sys.exit(1)

    print("All shaders validated successfully")


validate_shaders()
