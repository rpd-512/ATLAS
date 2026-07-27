from sys import argv
import json

if len(argv) != 2:
    print("Usage: python3 netlist_reader.py <netlist_file>")
    exit(1)

# load the netlist file
netlist_file = argv[1]
with open(netlist_file, 'r') as f:
    netlist_data = json.load(f)

print("netlist data loaded successfully.")

modules = netlist_data.get("modules", {})

# Pin names Yosys typically doesn't mark as inputs in port_directions
# fallback -- used only if a cell has no "port_directions" entry.
OUTPUT_PIN_NAMES = {"Y", "X", "Q", "QN", "Z", "CO", "SO", "S"}

for m in modules:
    print()
    print(f"Module: {m}")

    in_pin = []
    out_pin = []

    ports = modules[m].get('ports', {})
    for p in ports:
        if ports[p]["direction"] == "input":
            in_pin += ports[p]["bits"]
        elif ports[p]["direction"] == "output":
            out_pin += ports[p]["bits"]

    print(f"Input Pins: {in_pin}")
    print(f"Output Pins: {out_pin}")
    print()
    txt = "abcdefghijklmnopqrstuvwxyz".upper()
    tcnt = 0
    cells = modules[m].get('cells', {})
    for c in cells:
        cell_name = cells[c]['type'].split('__')[-1] if '__' in cells[c]['type'] else cells[c]['type']
        connections = cells[c]['connections']
        port_dirs = cells[c].get('port_directions', {})

        in_nets = []
        out_net = None

        for pin, bits in connections.items():
            direction = port_dirs.get(pin)
            if direction is None:
                direction = "output" if pin.upper() in OUTPUT_PIN_NAMES else "input"

            for b in bits:
                if direction == "output":
                    out_net = b
                else:
                    in_nets.append(b)

        args_str = ", ".join(str(n) for n in in_nets)
        print("\nGate",txt[tcnt])
        print(f"{out_net} = {cell_name}({args_str})")
        tcnt += 1