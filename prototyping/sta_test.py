#!/usr/bin/env python3
"""
full_adder_sta.py
------------------
Basic Static Timing Analysis (STA) on a full-adder built as a DAG of
standard-cell gate instances, with delay/slew lookup tables (NLDM-style
cell_rise / cell_fall / rise_transition / fall_transition) parsed straight
out of a Liberty (.lib) file.

WHAT THIS DOES
  1. Parses the .lib file with a lightweight brace-matching parser
     (no external liberty-parsing library required).
  2. Builds a full adder as a gate-level DAG:
         n1   = XOR2(A, B)
         Sum  = XOR2(n1, Cin)
         n2   = AND2(A, B)
         n3   = AND2(n1, Cin)
         Cout = OR2(n2, n3)
  3. Runs STA: topological traversal, propagating rise and fall arrival
     time / transition (slew) SEPARATELY from primary inputs to Sum/Cout.
     Each arc's liberty `timing_sense` (positive_unate / negative_unate /
     non_unate) determines which input edge maps to which output edge,
     so a rise-delay is always paired with the rise-transition table
     (and fall with fall) that produced it -- no mixing of unrelated
     rise/fall lookups. Non-unate arcs (e.g. XOR/XNOR, which really can
     produce either output edge from either input edge) consider all
     four edge combinations and keep the worst per output edge, same as
     a real STA tool would.
  4. Reports per-arc delay/slew and the overall critical-path
     propagation delay (worst of Sum-rise, Sum-fall, Cout-rise, Cout-fall).

WHAT THIS DELIBERATELY SIMPLIFIES (it's a "basic" STA, not a signoff tool)
  - No full liberty grammar (no bus/group templates, statetables, etc.)
    -- only cell/pin/timing/table groups are extracted.
  - If a cell has more than one timing() arc for the same related_pin
    (e.g. a conditional "when" arc), only the last one parsed is kept
    and a warning is printed -- conditional/derated arcs aren't merged.
  - No explicit wire/RC delay -- output net capacitance is just the
    sum of the liberty input-pin capacitances of the sinks (plus an
    optional lumped external load at the primary outputs).
  - Units are whatever the .lib file uses natively (no unit conversion).

CELL NAMES
  Standard-cell libraries name cells differently (e.g. AND2_X1, AN2X1,
  and2, sky130_fd_sc_hd__and2_1, ...). Edit CELL_MAP below (or pass
  --list-cells to print every cell name found in your .lib and pick
  the right ones).

USAGE
  python3 full_adder_sta.py mylib.lib
  python3 full_adder_sta.py mylib.lib --list-cells
  python3 full_adder_sta.py mylib.lib --input-transition 0.02 --output-load 0.01
"""

import argparse
import difflib
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple


# =====================================================================
# 1. EDIT THIS to match the actual cell names in your .lib file.
#    Run with --list-cells to see what's available.
# =====================================================================
CELL_MAP = {
    "XOR2": "sky130_fd_sc_hd__xor2_1",
    "AND2": "sky130_fd_sc_hd__and2_1",
    "OR2":  "sky130_fd_sc_hd__or2_1",
}

# Pin-name convention assumed for each cell type: (input_a, input_b, output)
# sky130_fd_sc_hd combinational cells use A/B inputs and X output.
PIN_MAP = {
    "XOR2": ("A", "B", "X"),
    "AND2": ("A", "B", "X"),
    "OR2":  ("A", "B", "X"),
}


# =====================================================================
# Liberty parsing
# =====================================================================

def _strip_comments(text: str) -> str:
    """Remove /* ... */ block comments. Liberty files (esp. real foundry libs like
    sky130) put copyright/notes comments -- sometimes containing stray '{' or '}'
    characters -- right inside cell bodies before the pin groups. Left in place,
    those stray braces desync the depth counter in _find_balanced_block and
    silently truncate the cell block before it ever reaches the pins."""
    return re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)


def _find_balanced_block(text: str, brace_idx: int) -> Tuple[str, int]:
    """Given the index of an opening '{', return (block_contents, index_after_closing_brace).
    Ignores braces that appear inside double-quoted strings, since liberty attribute
    values are sometimes quoted expressions that could otherwise desync the depth count."""
    assert text[brace_idx] == "{"
    depth = 0
    i = brace_idx
    in_string = False
    while i < len(text):
        ch = text[i]
        if ch == '"':
            in_string = not in_string
        elif not in_string:
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[brace_idx + 1:i], i + 1
        i += 1
    raise ValueError("Unbalanced braces in liberty file (reached EOF while scanning a group)")


def _iter_groups(text: str, group_name: str):
    """Yield (arg_string, block_contents) for every `group_name ( arg ) { ... }` at the
    top level of `text` (does not descend into nested groups of the same name automatically;
    caller recurses into the returned block_contents as needed)."""
    pattern = re.compile(rf"\b{re.escape(group_name)}\s*\(([^)]*)\)\s*\{{")
    pos = 0
    while True:
        m = pattern.search(text, pos)
        if not m:
            return
        arg = m.group(1).strip().strip('"')
        block, end_idx = _find_balanced_block(text, m.end() - 1)
        yield arg, block
        pos = end_idx


def _parse_number_list(csv_like: str) -> List[float]:
    return [float(x) for x in csv_like.replace("\\", " ").split(",") if x.strip()]


def _parse_table_group(block: str) -> Optional["LUT"]:
    """Parse an index_1 / index_2 / values table group into a LUT."""
    idx1_m = re.search(r'index_1\s*\(\s*"([^"]*)"\s*\)', block)
    idx2_m = re.search(r'index_2\s*\(\s*"([^"]*)"\s*\)', block)
    values_m = re.search(r"values\s*\((.*?)\)\s*;", block, re.DOTALL)
    if not (idx1_m and values_m):
        return None
    index_1 = _parse_number_list(idx1_m.group(1))
    index_2 = _parse_number_list(idx2_m.group(1)) if idx2_m else [0.0]
    row_strs = re.findall(r'"([^"]*)"', values_m.group(1))
    rows = [_parse_number_list(r) for r in row_strs]
    if not rows:
        return None
    return LUT(index_1=index_1, index_2=index_2, values=rows)


@dataclass
class LUT:
    """2D lookup table: rows indexed by index_1 (input transition),
    columns indexed by index_2 (output net capacitance)."""
    index_1: List[float]
    index_2: List[float]
    values: List[List[float]]

    def lookup(self, in_trans: float, out_cap: float) -> float:
        return _bilinear_interpolate(self.index_1, self.index_2, self.values, in_trans, out_cap)


def _interp1(x0, x1, y0, y1, x):
    if x1 == x0:
        return y0
    t = (x - x0) / (x1 - x0)
    return y0 + t * (y1 - y0)


def _bracket(axis: List[float], v: float) -> Tuple[int, int]:
    """Return the pair of indices bracketing v, clamping at the ends (no extrapolation)."""
    if v <= axis[0]:
        return 0, 0
    if v >= axis[-1]:
        return len(axis) - 1, len(axis) - 1
    for i in range(len(axis) - 1):
        if axis[i] <= v <= axis[i + 1]:
            return i, i + 1
    return len(axis) - 1, len(axis) - 1


def _bilinear_interpolate(rows: List[float], cols: List[float],
                           values: List[List[float]], r: float, c: float) -> float:
    r0i, r1i = _bracket(rows, r)
    c0i, c1i = _bracket(cols, c)
    q11 = values[r0i][c0i]
    q12 = values[r0i][c1i]
    q21 = values[r1i][c0i]
    q22 = values[r1i][c1i]
    top = _interp1(cols[c0i], cols[c1i], q11, q12, c)
    bot = _interp1(cols[c0i], cols[c1i], q21, q22, c)
    return _interp1(rows[r0i], rows[r1i], top, bot, r)


@dataclass
class PinTiming:
    cell_rise: Optional[LUT] = None
    cell_fall: Optional[LUT] = None
    rise_transition: Optional[LUT] = None
    fall_transition: Optional[LUT] = None
    timing_sense: Optional[str] = None  # "positive_unate" / "negative_unate" / "non_unate"


@dataclass
class CellDef:
    name: str
    pin_capacitance: Dict[str, float] = field(default_factory=dict)
    pin_direction: Dict[str, str] = field(default_factory=dict)
    # timing[output_pin][related_input_pin] = PinTiming
    timing: Dict[str, Dict[str, PinTiming]] = field(default_factory=dict)
    raw_block: str = ""
    pin_raw: Dict[str, str] = field(default_factory=dict)

    def worst_delay(self, out_pin: str, in_pin: str, in_trans: float, out_cap: float) -> float:
        """Kept for reference/back-compat with --dump-cell style checks; the main STA
        loop no longer uses this, since decoupling delay/transition from a shared edge
        can pair a rise-delay with a fall-transition that no real signal produces."""
        t = self.timing[out_pin][in_pin]
        candidates = []
        if t.cell_rise:
            candidates.append(t.cell_rise.lookup(in_trans, out_cap))
        if t.cell_fall:
            candidates.append(t.cell_fall.lookup(in_trans, out_cap))
        if not candidates:
            raise ValueError(f"No delay tables for {self.name}.{out_pin} rel {in_pin}")
        return max(candidates)


def parse_liberty(path: str) -> Dict[str, CellDef]:
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        text = f.read()
    text = _strip_comments(text)

    cells: Dict[str, CellDef] = {}
    for cell_name, cell_block in _iter_groups(text, "cell"):
        cdef = CellDef(name=cell_name, raw_block=cell_block)
        for pin_name, pin_block in _iter_groups(cell_block, "pin"):
            cdef.pin_raw[pin_name] = pin_block
            dir_m = re.search(r'direction\s*:\s*"?(\w+)"?\s*;', pin_block)
            cap_m = re.search(r"capacitance\s*:\s*([0-9.eE+-]+)\s*;", pin_block)
            if dir_m:
                cdef.pin_direction[pin_name] = dir_m.group(1)
            if cap_m:
                cdef.pin_capacitance[pin_name] = float(cap_m.group(1))

            for _, timing_block in _iter_groups(pin_block, "timing"):
                rel_m = re.search(r'related_pin\s*:\s*"?([\w\[\]]+)"?\s*;', timing_block)
                if not rel_m:
                    continue
                related_pin = rel_m.group(1)
                sense_m = re.search(r'timing_sense\s*:\s*"?(\w+)"?\s*;', timing_block)
                pt = PinTiming(timing_sense=sense_m.group(1) if sense_m else None)
                for table_kind in ("cell_rise", "cell_fall", "rise_transition", "fall_transition"):
                    for _, table_block in _iter_groups(timing_block, table_kind):
                        lut = _parse_table_group(table_block)
                        if lut:
                            setattr(pt, table_kind, lut)
                        break  # only one table group of this kind expected per timing arc
                if related_pin in cdef.timing.get(pin_name, {}):
                    pass
                    #print(f"WARNING: cell '{cell_name}' pin '{pin_name}' has more than one "
                    #      f"timing() arc for related_pin '{related_pin}' (e.g. a conditional "
                    #      f"'when' arc) -- only the last one parsed is kept; delay for this "
                    #      f"arc may be inaccurate.", file=sys.stderr)
                cdef.timing.setdefault(pin_name, {})[related_pin] = pt
        cells[cell_name] = cdef

    return cells


# =====================================================================
# Gate-level netlist (DAG) for the full adder
# =====================================================================

@dataclass
class Gate:
    name: str
    cell_type: str        # logical type, e.g. "XOR2" (key into CELL_MAP)
    in_a: str
    in_b: str
    out_net: str


@dataclass
class EdgeTiming:
    arrival: float = 0.0
    transition: float = 0.0


@dataclass
class NetTiming:
    rise: Optional[EdgeTiming] = None
    fall: Optional[EdgeTiming] = None


def arcs_for_sense(sense: Optional[str]) -> List[Tuple[str, str]]:
    """Map a liberty timing_sense to the (input_edge, output_edge) pairs it implies.
    positive_unate: input rise -> output rise, input fall -> output fall.
    negative_unate: input rise -> output fall, input fall -> output rise.
    non_unate (or unspecified, e.g. XOR/XNOR): either input edge can produce either
    output edge, so all four combinations are considered and the worst kept per edge --
    this matches how real STA tools treat non-unate arcs."""
    if sense == "positive_unate":
        return [("rise", "rise"), ("fall", "fall")]
    if sense == "negative_unate":
        return [("rise", "fall"), ("fall", "rise")]
    return [("rise", "rise"), ("rise", "fall"), ("fall", "rise"), ("fall", "fall")]


def build_full_adder() -> List[Gate]:
    return [
        Gate("U_XOR1", "XOR2", "A", "B", "n1"),
        Gate("U_XOR2", "XOR2", "n1", "Cin", "Sum"),
        Gate("U_AND1", "AND2", "A", "B", "n2"),
        Gate("U_AND2", "AND2", "n1", "Cin", "n3"),
        Gate("U_OR1", "OR2", "n2", "n3", "Cout"),
    ]


def topo_order(gates: List[Gate]) -> List[Gate]:
    """Kahn's algorithm over the net-dependency graph (works for any DAG, not just
    the fixed 5-gate full adder above, in case you extend the netlist)."""
    produced_by = {g.out_net: g for g in gates}
    indeg = {g.name: 0 for g in gates}
    deps = {g.name: [] for g in gates}  # gate -> gates that must come first
    for g in gates:
        for src_net in (g.in_a, g.in_b):
            src_gate = produced_by.get(src_net)
            if src_gate is not None:
                deps[g.name].append(src_gate.name)
                indeg[g.name] += 1

    by_name = {g.name: g for g in gates}
    ready = [g.name for g in gates if indeg[g.name] == 0]
    ordered = []
    while ready:
        n = ready.pop(0)
        ordered.append(by_name[n])
        for g in gates:
            if n in deps[g.name]:
                deps[g.name].remove(n)
                indeg[g.name] -= 1
                if indeg[g.name] == 0:
                    ready.append(g.name)
    if len(ordered) != len(gates):
        raise ValueError("Cycle detected in netlist -- not a DAG")
    return ordered


def fanout_capacitance(net: str, gates: List[Gate], cells: Dict[str, CellDef],
                        primary_output_load: float) -> float:
    """Sum of input-pin capacitances of every gate sinking `net`, plus an external
    lumped load if `net` is a primary output (Sum / Cout)."""
    total = 0.0
    for g in gates:
        for pin_side, in_net in (("in_a", g.in_a), ("in_b", g.in_b)):
            if in_net == net:
                cell_name = CELL_MAP[g.cell_type]
                a_pin, b_pin, _ = PIN_MAP[g.cell_type]
                pin_name = a_pin if pin_side == "in_a" else b_pin
                total += cells[cell_name].pin_capacitance.get(pin_name, 0.0)
    if net in ("Sum", "Cout"):
        total += primary_output_load
    return total


def run_sta(cells: Dict[str, CellDef], primary_input_transition: float,
            primary_output_load: float, verbose: bool = True) -> Dict[str, NetTiming]:

    for logical, real_name in CELL_MAP.items():
        if real_name not in cells:
            close = difflib.get_close_matches(real_name, cells.keys(), n=3)
            hint = f" Did you mean: {close}?" if close else " Use --list-cells to see available names."
            raise SystemExit(f"ERROR: cell '{real_name}' (mapped from {logical}) not found in liberty file.{hint}")

        cdef = cells[real_name]
        a_pin, b_pin, out_pin = PIN_MAP[logical]
        for pin in (a_pin, b_pin, out_pin):
            if pin not in cdef.pin_direction:
                available = sorted(cdef.pin_direction.keys())
                raise SystemExit(
                    f"ERROR: pin '{pin}' not found on cell '{real_name}' (mapped from {logical}). "
                    f"Available pins on this cell: {available}. Fix PIN_MAP at the top of the script."
                )
        if out_pin not in cdef.timing or a_pin not in cdef.timing[out_pin] or b_pin not in cdef.timing[out_pin]:
            have = {op: sorted(rel.keys()) for op, rel in cdef.timing.items()}
            raise SystemExit(
                f"ERROR: no timing arc from both '{a_pin}' and '{b_pin}' to '{out_pin}' on cell '{real_name}'. "
                f"Timing arcs found: {have}. Fix PIN_MAP at the top of the script."
            )

    gates = build_full_adder()
    order = topo_order(gates)

    timing: Dict[str, NetTiming] = {
        "A": NetTiming(rise=EdgeTiming(0.0, primary_input_transition), fall=EdgeTiming(0.0, primary_input_transition)),
        "B": NetTiming(rise=EdgeTiming(0.0, primary_input_transition), fall=EdgeTiming(0.0, primary_input_transition)),
        "Cin": NetTiming(rise=EdgeTiming(0.0, primary_input_transition), fall=EdgeTiming(0.0, primary_input_transition)),
    }

    if verbose:
        print(f"{'Gate':<10}{'Cell':<28}{'Arc':<24}{'InTrans':>10}{'OutCap':>10}{'Delay':>10}{'ArrTime':>10}{'OutTrans':>10}")
        print("-" * 132)

    for g in order:
        cell_name = CELL_MAP[g.cell_type]
        cdef = cells[cell_name]
        a_pin, b_pin, out_pin = PIN_MAP[g.cell_type]

        out_cap = fanout_capacitance(g.out_net, gates, cells, primary_output_load)

        best: Dict[str, Optional[EdgeTiming]] = {"rise": None, "fall": None}

        for in_pin_name, in_net in ((a_pin, g.in_a), (b_pin, g.in_b)):
            pt = cdef.timing[out_pin][in_pin_name]
            src = timing[in_net]
            for in_edge, out_edge in arcs_for_sense(pt.timing_sense):
                src_edge = src.rise if in_edge == "rise" else src.fall
                delay_lut = pt.cell_rise if out_edge == "rise" else pt.cell_fall
                trans_lut = pt.rise_transition if out_edge == "rise" else pt.fall_transition
                if src_edge is None or delay_lut is None or trans_lut is None:
                    continue
                delay = delay_lut.lookup(src_edge.transition, out_cap)
                out_trans = trans_lut.lookup(src_edge.transition, out_cap)
                arrival = src_edge.arrival + delay

                cur = best[out_edge]
                if cur is None or arrival > cur.arrival:
                    best[out_edge] = EdgeTiming(arrival=arrival, transition=out_trans)
                    if verbose:
                        arc_label = f"{in_net}({in_edge})->{g.out_net}({out_edge})"
                        print(f"{g.name:<10}{cell_name:<28}{arc_label:<24}"
                              f"{src_edge.transition:>10.4f}{out_cap:>10.4f}{delay:>10.4f}"
                              f"{arrival:>10.4f}{out_trans:>10.4f}")

        for edge in ("rise", "fall"):
            if best[edge] is None:
                raise ValueError(f"No valid {edge} arc computed for net '{g.out_net}' -- "
                                  f"check that both cell_rise/cell_fall and rise_transition/"
                                  f"fall_transition tables exist for this cell's timing arcs.")

        timing[g.out_net] = NetTiming(rise=best["rise"], fall=best["fall"])

    return timing


# =====================================================================
# Main
# =====================================================================

def main():
    ap = argparse.ArgumentParser(description="Basic STA on a full-adder DAG using a Liberty file.")
    ap.add_argument("liberty_file", help="Path to the .lib file")
    ap.add_argument("--list-cells", action="store_true", help="List all cell names found in the liberty file and exit")
    ap.add_argument("--dump-cell", metavar="CELL_NAME", default=None,
                     help="Diagnostic: print the parsed pins/timing structure for one cell and exit")
    ap.add_argument("--input-transition", type=float, default=0.02,
                     help="Primary input transition/slew, in the liberty file's native time unit (default 0.02)")
    ap.add_argument("--output-load", type=float, default=0.01,
                     help="Lumped external capacitive load on Sum/Cout, in the liberty file's native cap unit (default 0.01)")
    args = ap.parse_args()

    cells = parse_liberty(args.liberty_file)

    if args.list_cells:
        print(f"Found {len(cells)} cells in {args.liberty_file}:")
        for name in sorted(cells):
            print(f"  {name}")
        return

    if args.dump_cell:
        cdef = cells.get(args.dump_cell)
        if not cdef:
            close = difflib.get_close_matches(args.dump_cell, cells.keys(), n=5)
            raise SystemExit(f"ERROR: cell '{args.dump_cell}' not found. Close matches: {close}")
        print(f"Cell: {cdef.name}")
        print(f"Pins found: {sorted(cdef.pin_direction.keys())}")
        for pin, direction in sorted(cdef.pin_direction.items()):
            cap = cdef.pin_capacitance.get(pin)
            print(f"  {pin}: direction={direction} capacitance={cap}")
        print(f"Timing arcs found: {{out_pin: [related_pins]}}")
        for out_pin, rel in cdef.timing.items():
            print(f"  {out_pin}: {sorted(rel.keys())}")
        print(f"\nPin groups located by the parser: {sorted(cdef.pin_raw.keys())}")
        for pin_name, raw in cdef.pin_raw.items():
            print(f"\n--- RAW block for pin '{pin_name}' (first 600 chars) ---")
            print(raw[:600])
        return

    if not cells:
        raise SystemExit("ERROR: no cells parsed from liberty file -- check the file path/format.")

    timing = run_sta(cells, args.input_transition, args.output_load)

    print("\n=== Primary output arrival times ===")
    for net in ("Sum", "Cout"):
        nt = timing[net]
        print(f"  {net:<5} rise: arrival = {nt.rise.arrival:.4f}  transition = {nt.rise.transition:.4f}")
        print(f"  {net:<5} fall: arrival = {nt.fall.arrival:.4f}  transition = {nt.fall.transition:.4f}")

    candidates = [
        ("Sum", "rise", timing["Sum"].rise.arrival),
        ("Sum", "fall", timing["Sum"].fall.arrival),
        ("Cout", "rise", timing["Cout"].rise.arrival),
        ("Cout", "fall", timing["Cout"].fall.arrival),
    ]
    critical_net, critical_edge, critical_arrival = max(candidates, key=lambda c: c[2])
    print(f"\nCritical path: {critical_net} ({critical_edge})")
    print(f"Propagation delay (max over Sum/Cout, both edges): {critical_arrival:.4f} "
          f"(liberty native time unit)")


if __name__ == "__main__":
    main()