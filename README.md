# ATLAS: Automated Timing, Logic, Area and Statistics

<p align="center">
  <img src="images/ATLAS.png" alt="ATLAS Logo" width="300"/>
</p>

ATLAS is an open-source tool for fast analysis and evaluation of synthesized digital hardware designs.

ATLAS takes a **technology-mapped netlist** together with a standard-cell Liberty library and performs the core analysis pipeline natively, extracting hardware metrics including:

* **Timing** — propagation delay and static timing analysis (STA)
* **Logic** — functional and soft logic evaluation
* **Area** — standard-cell-based circuit area estimation
* **Power** — static, switching, internal, and total power estimation
* **Statistics** — summarized circuit and analysis information

ATLAS uses **Yosys** to synthesize and technology-map the original Verilog/SystemVerilog design into a JSON netlist. Once the mapped netlist has been generated, timing, logic, area, and power analysis are performed by ATLAS.

The current workflow has been developed and tested using **Yosys 0.9**.

## Status

The core ATLAS analysis engine is **complete and functional**.

* ✅ Functional logic evaluation
* ✅ Soft logic evaluation
* ✅ Liberty standard-cell parsing
* ✅ Area computation
* ✅ Static timing analysis (STA)
* ✅ NLDM-based delay and slew estimation
* ✅ Static/leakage power estimation
* ✅ Switching power estimation
* ✅ Internal power estimation
* ✅ Total circuit power estimation

The primary remaining work is **documentation and API cleanup**.

Until comprehensive documentation is available, developers interested in using ATLAS programmatically can refer to **`main.cpp`**, which demonstrates the current analysis pipeline and API.

## Workflow

The current ATLAS workflow consists of two stages:

```text
SystemVerilog / Verilog
          │
          ▼
        Yosys
   synthesis + mapping
          │
          ▼
    JSON Netlist
          │
          │ + Liberty Library
          ▼
        ATLAS
          │
          ├── Logic Evaluation
          ├── Area Analysis
          ├── Static Timing Analysis
          ├── Power Analysis
          └── Statistics
```

### 1. Write the RTL design

Create your design as a `.sv` file.

For the current workflow, the RTL file should satisfy the following restrictions:

* The file should contain **only one module**.
* The module should **not instantiate another user-defined module**.
* The module name must be known when configuring the Yosys synthesis script.

For example:

```systemverilog
module LOD8(
    input  logic [7:0] in,
    output logic [2:0] out
);

    // design

endmodule
```

### 2. Configure `script.ys`

Open `script.ys` and specify the RTL file and its module name.

The synthesis script should read the design, synthesize it using Yosys, technology-map it against the desired standard-cell library, and generate the JSON netlist consumed by ATLAS.

Make sure the filename and top-level module name in `script.ys` correspond to your design.

### 3. Run Yosys

Run the synthesis script with:

```bash
yosys -s script.ys
```

This generates the technology-mapped JSON netlist required by ATLAS.

> **Note:** The current workflow has been developed and tested using **Yosys 0.9**. Other Yosys versions may work, but have not necessarily been tested against the current parser and workflow.

### 4. Run ATLAS

Once the JSON netlist has been generated, pass both the netlist and the corresponding Liberty library to ATLAS:

```bash
./atlas <netlist.json> <liberty.lib>
```

For example:

```bash
./atlas netlist.json liberty/sky130_fd_sc_hd__tt_025C_1v80.lib
```

ATLAS will parse the mapped circuit and Liberty characterization data and run its analysis pipeline.

Typical output includes:

```text
=== Functional evaluation ===
...

=== Soft evaluation ===
...

=== Area ===
...

=== Static timing analysis ===
...

=== Power ===
...

=== Timing breakdown ===
...
```

## Features

### Logic Evaluation

ATLAS evaluates mapped combinational circuits using Boolean functions extracted directly from the standard-cell Liberty definitions.

Both conventional Boolean evaluation and continuous/soft logic evaluation are supported.

### Static Timing Analysis

ATLAS implements graph-based static timing analysis, including:

* Input and output transition propagation
* Output capacitance calculation
* NLDM-based delay and slew estimation
* Arrival-time propagation
* Critical-path identification
* Required-time and slack infrastructure

Timing characteristics are derived from the supplied standard-cell Liberty library.

### Area Analysis

Circuit area is calculated by accumulating the physical area of the technology-mapped standard cells using values provided by the Liberty library.

### Power Analysis

ATLAS estimates:

* Static/leakage power
* Switching power
* Internal cell power
* Total circuit power

Power estimation uses characterization information extracted from the supplied Liberty library together with circuit capacitance and activity information.

## Dependencies

### Yosys

Yosys is required to synthesize and technology-map the original RTL design into the JSON netlist consumed by ATLAS.

ATLAS has currently been developed and tested with:

```text
Yosys 0.9
```

### C++ Dependencies

ATLAS additionally uses:

* [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing and netlist processing
* [Eigen3](https://eigen.tuxfamily.org/) — fitting delay/slew surfaces from Liberty NLDM tables using least-squares
* [indicators](https://github.com/p-ranav/indicators) — terminal progress bars and CLI progress reporting

On Ubuntu, the required development packages can be installed with:

```bash
sudo apt install nlohmann-json3-dev libeigen3-dev
```

To install `indicators` system-wide:

```bash
git clone https://github.com/p-ranav/indicators.git
cd indicators
sudo cp -r include/indicators /usr/local/include/
```

## Building

Build ATLAS with:

```bash
make
```

This produces the `atlas` executable in the project root.

For a debug build with symbols and without optimization:

```bash
make debug
```

To remove build artifacts:

```bash
make clean
```

## Quick Start

The complete workflow is:

```text
1. Write the design in a .sv file.

2. Ensure the file contains one module and does not instantiate
   other user-defined modules.

3. Set the RTL filename and module name in script.ys.

4. Synthesize and technology-map the design:

       yosys -s script.ys

5. Run ATLAS on the generated netlist:

       ./atlas <netlist.json> <liberty.lib>

6. Read the generated timing, logic, area, power, and
   performance results.
```

## Documentation

The ATLAS analysis engine is complete, while comprehensive documentation is still being developed.

Planned documentation will cover:

* Netlist format and parsing
* Liberty parsing
* Logic and soft-logic evaluation
* Static timing analysis
* NLDM processing
* Power estimation
* Programmatic API usage
* Yosys synthesis and technology-mapping configuration
* Integration into optimization and design-space exploration workflows

Until then, **`main.cpp` serves as the primary programmatic usage example** and demonstrates how the individual ATLAS analysis passes are invoked.

## Contributing

ATLAS is functional, but its documentation, API, and developer experience are still evolving.

Issues, bug reports, benchmarks, support for additional Liberty libraries, testing with additional Yosys versions, and pull requests are welcome.

## License

MIT