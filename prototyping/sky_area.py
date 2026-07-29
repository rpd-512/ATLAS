#!/usr/bin/env python3

import re
import sys

if len(sys.argv) != 2:
    print(f"Usage: python3 {sys.argv[0]} <liberty_file.lib>")
    sys.exit(1)

liberty_file = sys.argv[1]

with open(liberty_file, "r") as f:
    data = f.read()

# Find the beginning of every cell block.
cell_pattern = re.compile(r'\bcell\s*\(\s*"?([^")]+)"?\s*\)\s*\{')

cells = []

for match in cell_pattern.finditer(data):
    cell_name = match.group(1).strip()
    start = match.end()

    # Find matching closing brace for this cell.
    depth = 1
    i = start

    while i < len(data) and depth > 0:
        if data[i] == "{":
            depth += 1
        elif data[i] == "}":
            depth -= 1
        i += 1

    cell_block = data[start:i - 1]

    # area : 3.7536;
    area_match = re.search(
        r'\barea\s*:\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*;',
        cell_block
    )

    if area_match:
        area = float(area_match.group(1))
        cells.append((cell_name, area))

# Sort alphabetically
cells.sort(key=lambda x: x[0])

print(f"{'Cell':50} {'Area':>12}")
print("-" * 63)

for name, area in cells:
    print(f"{name:50} {area:12.6f}")

print("-" * 63)
print(f"Total cells: {len(cells)}")