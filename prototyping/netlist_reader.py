import json
import re
from sys import argv

# ============================================================================
# Yosys JSON netlist reader + boolean evaluator.
#
# Signal/chromosome layout (mirrors the C++ CGP signal-indexing convention
# in cgp_utils.h, extended to support gates with multiple outputs):
#
#   bits = [ input bits ..., output bits ..., functionality bits ... ]
#
# "functionality bits" is a FIXED-STRIDE block: exactly MAX_OUTPUTS entries
# per gate/node, in node order, so node i's outputs always live at
# functionality[MAX_OUTPUTS*i : MAX_OUTPUTS*i + MAX_OUTPUTS]. Nodes with
# fewer real outputs than MAX_OUTPUTS get placeholder IDs in the unused
# slots (never referenced by anything, so harmless).
#
# gates = [ [out0, out1, out2, cell_type, in0, in1, ...], ... ]
#   one entry per cell, in the same order as the functionality blocks.
# ============================================================================

# Pin names Yosys typically doesn't mark as inputs in port_directions
# fallback -- used only if a cell has no "port_directions" entry.
OUTPUT_PIN_NAMES = {"Y", "X", "Q", "QN", "Z", "CO", "COUT", "SO", "S", "SUM"}

MAX_OUTPUTS = 3  # fixed stride: every node reserves this many signal slots,
                 # regardless of how many outputs its gate type really has.


def is_const(bit) -> bool:
    # Yosys represents constants either as literal ints or as strings
    # ("0"/"1"/"x"/"z") inside a bits array. Treat all of these as
    # constants, not real net IDs, so they're excluded from the remap.
    return isinstance(bit, str)


def parse_netlist(netlist_path: str):
    with open(netlist_path, 'r') as f:
        netlist_data = json.load(f)

    modules = netlist_data.get("modules", {})
    result = {}

    for m in modules:
        in_pin = []
        out_pin = []

        ports = modules[m].get('ports', {})
        for p in ports:
            if ports[p]["direction"] == "input":
                in_pin += ports[p]["bits"]
            elif ports[p]["direction"] == "output":
                out_pin += ports[p]["bits"]
        print("Inputs:", in_pin)
        print("Outputs:", out_pin)
        cells = modules[m].get('cells', {})

        # ---- pass 1: collect every real bit + per-cell in/out nets ----
        all_bits = set()
        for b in in_pin + out_pin:
            if not is_const(b):
                all_bits.add(b)
        print("All bits:", all_bits)

        raw_nodes = []  # [(cell_type, in_nets, out_nets)]
        for c in cells:
            cell_type = cells[c]['type']
            cell_type = cell_type.split('__')[-1] if '__' in cell_type else cell_type
            connections = cells[c]['connections']
            port_dirs = cells[c].get('port_directions', {})

            in_nets = []
            out_nets = []

            for pin, conn_bits in connections.items():
                direction = port_dirs.get(pin)
                print(direction, pin, conn_bits)
                if direction is None:
                    direction = "output" if pin.upper() in OUTPUT_PIN_NAMES else "input"
                for b in conn_bits:
                    if direction == "output":
                        print("output", b)
                        out_nets.append(b)
                        if not is_const(b):
                            all_bits.add(b)
                    elif direction == "input":
                        print("input", b)
                        in_nets.append(b)
                        if not is_const(b):
                            all_bits.add(b)

            if len(out_nets) > MAX_OUTPUTS:
                raise ValueError(
                    f"Cell {c} ({cell_type}) has {len(out_nets)} outputs, "
                    f"exceeds MAX_OUTPUTS={MAX_OUTPUTS}"
                )
            raw_nodes.append((cell_type, in_nets, out_nets))

        # ---- pass 2: dense remap for all REAL signals seen so far ----
        remap = {old: new for new, old in enumerate(sorted(all_bits))}

        def r(bit):
            return bit if is_const(bit) else remap[bit]

        in_pin_r = [r(b) for b in in_pin]
        out_pin_r = [r(b) for b in out_pin]

        # ---- pass 3: build fixed-stride functionality blocks ----
        next_pad_id = len(remap)  # placeholder IDs continue past the dense real range
        functionality = []
        gates = []
        for cell_type, in_nets, out_nets in raw_nodes:
            out_nets_r = [r(b) for b in out_nets]
            while len(out_nets_r) < MAX_OUTPUTS:
                out_nets_r.append(next_pad_id)  # placeholder, not a real signal
                next_pad_id += 1

            functionality.extend(out_nets_r)
            in_nets_r = [r(b) for b in in_nets]
            gates.append([*out_nets_r, cell_type, *in_nets_r])

        bits = in_pin_r + out_pin_r + functionality

        result[m] = (bits, gates)

    return result


# ----------------------------------------------------------------------
# Boolean function table.
#
# Every entry returns a TUPLE, even single-output gates (e.g. NOR2
# returns (value,)); multi-output gates like ha_1/fa_1 return e.g.
# (sum, carry). Tuple order must match the order the cell's output pins
# were encountered in `connections`, i.e. must line up with out0/out1/out2
# in `gates`.
#
# NOTE: ha_1, fa_1, and maj3_1 are encoded from standard naming
# conventions, not verified against your actual SKY130 .lib -- worth a
# sanity check (pin order, polarity) before trusting results that depend
# on them.
# ----------------------------------------------------------------------
GATE_FUNCS = {
    "buf_1":     lambda a: (a,),
    "inv_1":     lambda a: (not a,),
    "clkinv_1":  lambda a: (not a,),
    "and2_1":    lambda a, b: (a and b,),
    "or2_1":     lambda a, b: (a or b,),
    "nand2_1":   lambda a, b: (not (a and b),),
    "nor2_1":    lambda a, b: (not (a or b),),
    "xor2_1":    lambda a, b: (a != b,),
    "xnor2_1":   lambda a, b: (a == b,),
    "nand3_1":   lambda a, b, c: (not (a and b and c),),
    "nor3_1":    lambda a, b, c: (not (a or b or c),),
    "and3_1":    lambda a, b, c: (a and b and c,),
    "or3_1":     lambda a, b, c: (a or b or c,),
    "or4_1":     lambda a, b, c, d: (a or b or c or d,),
    "and4_1":    lambda a, b, c, d: (a and b and c and d,),
    "nand4_1":   lambda a, b, c, d: (not (a and b and c and d),),
    "nor4_1":    lambda a, b, c, d: (not (a or b or c or d),),
    "mux2_1":    lambda a, b, s: (b if s else a,),
    "nand2b_1":  lambda a_n, b: (a_n or (not b),),
    "nor2b_1":   lambda a_n, b: ((not a_n) and (not b),),
    "a21oi_1":   lambda a1, a2, b1: (not ((a1 and a2) or b1),),
    "a211o_1":   lambda a1, a2, b1, c1: ((a1 and a2) or b1 or c1,),
    "a22oi_1":   lambda a1, a2, b1, b2: (not ((a1 and a2) or (b1 and b2)),),
    "a221o_1":   lambda a1, a2, b1, b2, c1: ((a1 and a2) or (b1 and b2) or c1,),
    "o21ai_1":   lambda a1, a2, b1: (not ((a1 or a2) and b1),),
    "o211a_1":   lambda a1, a2, b1, c1: (((a1 or a2) and b1) or c1,),
    "oai21_1":   lambda a1, a2, b1: (not ((a1 or a2) and b1),),
    "ha_1":      lambda a, b: (a != b, a and b),                        # (sum, carry)
    "fa_1":      lambda a, b, cin: (a ^ b ^ cin,
                                     (a and b) or (cin and (a ^ b))),   # (sum, carry)
    "maj3_1":    lambda a, b, c: ((a and b) or (b and c) or (a and c),),
    # Power-gating isolation buffer: functionally a passthrough of A;
    # SLEEP is a control pin, not part of the boolean logic, so it's
    # simply not consumed here.
    "lpflow_isobufsrc_1": lambda a, sleep: (a,),
}

# Drive strength (trailing _0, _1, _2, _4, _8, ...) doesn't change the
# boolean function of a SKY130 cell, only its sizing -- so strip it
# before looking up GATE_FUNCS, rather than requiring every strength
# variant to be listed individually (e.g. o21ai_0 falls back to o21ai_1).
_STRENGTH_SUFFIX_RE = re.compile(r"_\d+$")


def _base_cell_type(cell_type: str) -> str:
    return _STRENGTH_SUFFIX_RE.sub("", cell_type)


_GATE_FUNCS_BY_BASE = {_base_cell_type(k): v for k, v in GATE_FUNCS.items()}


def lookup_gate_func(cell_type: str):
    if cell_type in GATE_FUNCS:
        return GATE_FUNCS[cell_type]
    base = _base_cell_type(cell_type)
    if base in _GATE_FUNCS_BY_BASE:
        return _GATE_FUNCS_BY_BASE[base]
    raise KeyError(f"No boolean function defined for cell type '{cell_type}'")


def evaluate_circuit(bits, gates, num_inputs, num_outputs, input_values):
    assert len(input_values) == num_inputs, "input_values length must match num_inputs"

    input_signals = bits[:num_inputs]
    output_signals = bits[num_inputs:num_inputs + num_outputs]
    input_map = dict(zip(input_signals, input_values))

    # signal -> (node_index, port_index), only for real (non-placeholder) outputs
    signal_to_node_port = {}
    for node_idx, entry in enumerate(gates):
        out0, out1, out2, cell_type, *in_nets = entry
        for port_idx, out_sig in enumerate((out0, out1, out2)):
            signal_to_node_port[out_sig] = (node_idx, port_idx)

    node_cache = {}  # node_index -> tuple of computed output values

    def eval_node(node_idx):
        if node_idx in node_cache:
            return node_cache[node_idx]
        out0, out1, out2, cell_type, *in_nets = gates[node_idx]
        func = lookup_gate_func(cell_type)
        arg_vals = [value_of(s) for s in in_nets]
        result = func(*arg_vals)
        node_cache[node_idx] = result
        return result

    def value_of(signal):
        if is_const(signal):
            if signal == "1":
                return True
            if signal == "0":
                return False
            raise ValueError(f"Unresolvable constant bit: {signal!r}")

        if signal in input_map:
            return input_map[signal]

        if signal in signal_to_node_port:
            node_idx, port_idx = signal_to_node_port[signal]
            return eval_node(node_idx)[port_idx]

        raise KeyError(f"Signal {signal} is neither an input nor any gate's output")

    return [value_of(o) for o in output_signals]


if __name__ == "__main__":
    if len(argv) != 2:
        print("Usage: python3 netlist_reader.py <netlist_file>")
        exit(1)

    input_values = [0, 0, 1, 1, 1, 1, 0, 0]
    for name, (bits, gates) in parse_netlist(argv[1]).items():
        print(f"\nModule: {name}")
        print("Bits:", bits)
        print("Gates:", gates)

        result = evaluate_circuit(bits, gates, num_inputs=8, num_outputs=3,
                                   input_values=input_values[::-1])
        print("Input values:", "".join(str(int(x)) for x in input_values[::-1]))
        print("Output values:", "".join(str(int(x)) for x in result[::-1]))