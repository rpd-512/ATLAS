import json

# Pin names Yosys typically doesn't mark as inputs in port_directions
# fallback -- used only if a cell has no "port_directions" entry.
OUTPUT_PIN_NAMES = {"Y", "X", "Q", "QN", "Z", "CO", "SO", "S"}


def is_const(bit) -> bool:
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

        cells = modules[m].get('cells', {})

        # Collect every real bit referenced anywhere in the module, then
        # sort + enumerate to get a dense, gap-free 0..n-1 remap.
        all_bits = set()
        for b in in_pin + out_pin:
            if not is_const(b):
                all_bits.add(b)
        for c in cells:
            for conn_bits in cells[c]['connections'].values():
                for b in conn_bits:
                    if not is_const(b):
                        all_bits.add(b)

        remap = {old: new for new, old in enumerate(sorted(all_bits))}

        def r(bit):
            return bit if is_const(bit) else remap[bit]

        in_pin_r = [r(b) for b in in_pin]
        out_pin_r = [r(b) for b in out_pin]

        gates = []
        for c in cells:
            cell_type = cells[c]['type']
            cell_type = cell_type.split('__')[-1] if '__' in cell_type else cell_type
            connections = cells[c]['connections']
            port_dirs = cells[c].get('port_directions', {})

            in_nets = []
            out_net = None

            for pin, conn_bits in connections.items():
                direction = port_dirs.get(pin)
                if direction is None:
                    direction = "output" if pin.upper() in OUTPUT_PIN_NAMES else "input"

                for b in conn_bits:
                    if direction == "output":
                        out_net = r(b)
                    else:
                        in_nets.append(r(b))

            gates.append([out_net, cell_type, *in_nets])

        # Functionality bits = every remapped signal that's neither a
        # primary input nor a circuit output -- i.e. internal gate
        # outputs. Sorted for a stable, deterministic ordering.
        boundary = set(in_pin_r) | set(out_pin_r)
        functionality_bits = sorted(v for v in remap.values() if v not in boundary)

        bits = in_pin_r + out_pin_r + functionality_bits

        result[m] = (bits, gates)

    return result

GATE_FUNCS = {
    "buf_1":     lambda a: a,
    "inv_1":     lambda a: not a,
    "clkinv_1":  lambda a: not a,
    "and2_1":    lambda a, b: a and b,
    "or2_1":     lambda a, b: a or b,
    "nand2_1":   lambda a, b: not (a and b),
    "nor2_1":    lambda a, b: not (a or b),
    "xor2_1":    lambda a, b: a != b,
    "xnor2_1":   lambda a, b: a == b,
    "nand3_1":   lambda a, b, c: not (a and b and c),
    "nor3_1":    lambda a, b, c: not (a or b or c),
    "and3_1":    lambda a, b, c: a and b and c,
    "or3_1":     lambda a, b, c: a or b or c,
    "or4_1":     lambda a, b, c, d: a or b or c or d,
    "and4_1":    lambda a, b, c, d: a and b and c and d,
    "nand4_1":   lambda a, b, c, d: not (a and b and c and d),
    "nor4_1":    lambda a, b, c, d: not (a or b or c or d),
    "mux2_1":    lambda a, b, s: b if s else a,
    "nand2b_1":  lambda a_n, b: a_n or (not b),
    "nor2b_1":   lambda a_n, b: (not a_n) and (not b),
    "a21oi_1":   lambda a1, a2, b1: not ((a1 and a2) or b1),
    "a211o_1":   lambda a1, a2, b1, c1: (a1 and a2) or b1 or c1,
    "a22oi_1":   lambda a1, a2, b1, b2: not ((a1 and a2) or (b1 and b2)),
    "a221o_1":   lambda a1, a2, b1, b2, c1: (a1 and a2) or (b1 and b2) or c1,
    "o21ai_1":   lambda a1, a2, b1: not ((a1 or a2) and b1),
    "o211a_1":   lambda a1, a2, b1, c1: ((a1 or a2) and b1) or c1,
    "oai21_1":   lambda a1, a2, b1: not ((a1 or a2) and b1),
}


def evaluate_circuit(bits, gates, num_inputs, num_outputs, input_values):
    assert len(input_values) == num_inputs, "input_values length must match num_inputs"

    input_signals = bits[:num_inputs]
    output_signals = bits[num_inputs:num_inputs + num_outputs]  # derived, not passed in
    input_map = dict(zip(input_signals, input_values))

    gate_by_output = {}
    for entry in gates:
        out_net, cell_type, *in_nets = entry
        gate_by_output[out_net] = (cell_type, in_nets)

    cache = {}

    def value_of(signal):
        if is_const(signal):
            if signal == "1":
                return True
            if signal == "0":
                return False
            raise ValueError(f"Unresolvable constant bit: {signal!r}")

        if signal in cache:
            return cache[signal]

        if signal in input_map:
            val = input_map[signal]
        elif signal in gate_by_output:
            cell_type, in_nets = gate_by_output[signal]
            if cell_type not in GATE_FUNCS:
                raise KeyError(f"No boolean function defined for cell type '{cell_type}'")
            val = GATE_FUNCS[cell_type](*(value_of(s) for s in in_nets))
        else:
            raise KeyError(f"Signal {signal} is neither an input nor any gate's output")

        cache[signal] = val
        return val

    return [value_of(o) for o in output_signals]

if __name__ == "__main__":
    from sys import argv
    if len(argv) != 2:
        print("Usage: python3 netlist_reader.py <netlist_file>")
        exit(1)

    for name, (bits, gates) in parse_netlist(argv[1]).items():
        print(f"\nModule: {name}")
        print("Bits:", bits)
        print("Gates:", gates)

        result = evaluate_circuit(bits, gates, num_inputs=8, num_outputs=3,
                            input_values=[0,0,1,1,1,1,0,0][::-1])
        print(list(map(int, result))[::-1])
