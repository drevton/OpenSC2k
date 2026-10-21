#!/usr/bin/env python3
"""Bake the painted map icons with the shared Node.js/sharp packer."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
subprocess.run(["node", "tools/generate_map_icons.cjs", "--map-only"], cwd=ROOT, check=True)
