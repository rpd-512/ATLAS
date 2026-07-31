# ATLAS: Automated Timing, Logic, Area and Statistics

<p align="center">
  <img src="images/ATLAS.png" alt="ATLAS Logo" width="300"/>
</p>

ATLAS is an open-source tool for automating the analysis and evaluation of digital hardware designs.

The goal of ATLAS is to provide a **fast, self-contained** workflow for processing HDL designs and extracting useful hardware metrics such as:

- **Timing** — delay and static timing analysis
- **Logic** — synthesized logic and gate information
- **Area** — estimated circuit area
- **Power** — static and dynamic power estimation
- **Statistics** — summarized design and synthesis statistics

Unlike most open-source EDA flows, ATLAS aims to do this **natively** — without shelling out to external synthesis or analysis tools — so that design evaluation loops stay fast and the whole pipeline stays within a single lightweight binary.

## Status

- ✅ Logic evaluation — done
- ✅ Area computation — done
- 🚧 STA (static timing analysis) — in progress
- ⬜ Power estimation — not started

## Goals

- [ ] Native RTL parsing / elaboration for a practical subset of SystemVerilog and Verilog
- [ ] In-house logic synthesis and technology mapping against standard-cell liberty files
- [ ] Native static timing analysis (STA) via graph-based arrival/slack propagation
- [ ] Static and dynamic power estimation from liberty data
- [ ] Area and design statistics extraction
- [ ] JSON report generation (via [nlohmann/json](https://github.com/nlohmann/json))

## Dependencies

None of the external EDA toolchain (Yosys, OpenROAD) is required going forward. The third-party dependencies are:

- [nlohmann/json](https://github.com/nlohmann/json) — for parsing and generating JSON reports
- [Eigen3](https://eigen.tuxfamily.org/) — used to fit delay/slew surfaces from liberty NLDM tables via least-squares

## Building

```bash
sudo apt install nlohmann-json3-dev libeigen3-dev   # or install the headers manually

make
```

This produces an `atlas` binary in the project root.

For a debug build (with symbols, no optimization):

```bash
make debug
```

To clean build artifacts:

```bash
make clean
```

## Usage

Usage is being redesigned alongside the native engine and will be documented here once the first native synthesis/STA/power pass is working end-to-end.

```bash
./atlas <netlist.json> <liberty.lib>
```

## Contributing

ATLAS is early-stage and actively changing shape. Issues and PRs are welcome, but expect the core internals to be in flux.

## License

MIT