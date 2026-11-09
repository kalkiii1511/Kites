# CS2323 Computer Architecture Project Scope

The attached scope document is the working plan for the project. It defines the intended platform as an integrated computer architecture laboratory, built in stages around a shared execution and artifact model.

[Read the full scope document](CS2323_Computer_Architecture_Project_Scope.pdf).

## Planned modules

- Custom ISA designer and instruction encoder/decoder.
- Processor design studio with executable supported designs.
- Compiler and cross-compilation workflows, plus reconstructed C-like views for supported assembly.
- Beginner editor, execution controls, register and memory inspection.
- Cache and branch-prediction experiments with reproducible metrics.
- RTL simulation sandbox and virtual embedded-board demonstrations.
- Persistent projects and integration across the modules.

The scope allows an initial implementation to support a defined subset, but expects each core module to have meaningful functionality and a testable demonstration. Advanced compiler backends, predictors, cache hierarchies, board models, physical FPGA deployment, and course collaboration features are identified as stretch goals.

## Proposed sequence

1. **Architecture and foundations (weeks 1–2):** agree on module boundaries, shared schemas, the initial ISA, ownership, and validation approach.
2. **Core implementation (weeks 3–6):** establish working demonstrations across the core modules and persistence.
3. **Integration (weeks 7–9):** connect assembly, ISA, execution traces, synchronized views, metrics, RTL, and board workflows.
4. **Validation and delivery (weeks 10–12):** run regression and end-to-end validation, document assumptions, and prepare reproducible demonstrations.

The schedule is relative and should be adjusted once the course deadline, team capacity, available toolchains, and hardware access are confirmed. Performance figures should be labelled as simulated architectural results unless measured on physical hardware.

## Existing execution controls

The Kites simulator stops when the processor reaches the end of the loaded program. The web execution flow exposes address breakpoints; execution pauses before a selected instruction and reports the paused state. These controls satisfy the corresponding editor and memory-explorer scope items for the currently supported simulator workflow.
