#!/usr/bin/env python3
import re
import sys

def list_gates(lib_path):
    gates = []
    with open(lib_path, "r") as f:
        for line in f:
            m = re.search(r'cell\s*\(\s*"([^"]+)"\s*\)', line)
            if m:
                gates.append(m.group(1))
    return gates

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "liberty.lib"
    gates = list_gates(path)
    print(f"Found {len(gates)} gates:\n")
    for g in gates:
        print(g)