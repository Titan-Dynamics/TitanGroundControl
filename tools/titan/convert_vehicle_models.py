#!/usr/bin/env python3
"""Decimate Mission Planner's vehicle STL models for the Titan 3D map.

Source: the ``Resources/*.stl`` files of the TitanPlanner (Mission Planner) repository. Each
model is reduced to TARGET_TRIANGLES and written as a binary STL in its original frame
(millimetres, Mission Planner axes), so the 3D map loads it exactly like a custom STL: rotated
-90 degrees about Z and scaled to metres at load time.

Usage:
    python tools/titan/convert_vehicle_models.py --planner ../TitanPlanner

Requires ``numpy``, ``numpy-stl`` and ``pyfqmr`` (``pip install numpy numpy-stl pyfqmr``).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pyfqmr
from stl import Mode as StlMode
from stl import mesh as stl_mesh

# Mission Planner ships one copter mesh under several names; only the distinct files are converted.
MODELS = ("boat", "copter", "helicopter", "plane", "quadplane", "rover", "sub")
TARGET_TRIANGLES = 8000
OUTPUT_DIR = Path(__file__).resolve().parents[2] / "src" / "TitanMap3D" / "models"


def load_welded(path: Path) -> tuple[np.ndarray, np.ndarray]:
    """Return (vertices, faces) with vertices shared between triangles (STL stores them per facet)."""
    triangles = stl_mesh.Mesh.from_file(str(path)).vectors.astype(np.float64)
    corners = triangles.reshape(-1, 3)
    vertices, inverse = np.unique(np.round(corners, 5), axis=0, return_inverse=True)
    return vertices, inverse.reshape(-1, 3)


def decimate(vertices: np.ndarray, faces: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    if len(faces) <= TARGET_TRIANGLES:
        return vertices, faces
    simplifier = pyfqmr.Simplify()
    simplifier.setMesh(vertices, faces)
    simplifier.simplify_mesh(
        target_count=TARGET_TRIANGLES, aggressiveness=7, preserve_border=True, verbose=0
    )
    decimated_vertices, decimated_faces, _ = simplifier.getMesh()
    return decimated_vertices, decimated_faces


def write_binary_stl(path: Path, vertices: np.ndarray, faces: np.ndarray) -> None:
    out = stl_mesh.Mesh(np.zeros(len(faces), dtype=stl_mesh.Mesh.dtype))
    out.vectors[:] = vertices[faces]
    out.update_normals()
    out.save(str(path), mode=StlMode.BINARY)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--planner", type=Path, required=True, help="TitanPlanner repository root")
    parser.add_argument("--output", type=Path, default=OUTPUT_DIR)
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    for name in MODELS:
        source = args.planner / "Resources" / f"{name}.stl"
        vertices, faces = decimate(*load_welded(source))
        output = args.output / f"{name}.stl"
        write_binary_stl(output, vertices, faces)
        extent = (vertices.max(axis=0) - vertices.min(axis=0)) / 1000.0
        print(
            f"{name:11s} {len(faces):6d} triangles  {output.stat().st_size // 1024:5d} KB  "
            f"size (x y z m): {extent[0]:.2f} {extent[1]:.2f} {extent[2]:.2f}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
