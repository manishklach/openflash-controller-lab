# Changelog

All notable OpenFlash changes are recorded here. The project follows Semantic Versioning
for public milestones while the hardware ABI is versioned independently.

## [Unreleased]

### Added

- Reproducible, checksum-verified QEMU 11.0.2 build in GitHub Actions.
- Linux synchronous identify submission, phase-tag validation, and capacity discovery.
- Reusable Linux admin command submission with CIDs and SQ/CQ wrap handling.
- QTests for PCI discovery, lifecycle/reset, capabilities, and queue register validation.
- DMA QTests for identify, MSI-X, data integrity, discard, errors, and phase wrap.
- Interrupt-driven Linux admin completion draining with timeout synchronization.
- Admin-negotiated I/O queue with independent DMA rings and MSI-X vector 1.
- Linux `blk-mq` disk registration with tag-owned request completion.
- ABI v0.2 scatter-gather lists with up to 16 DMA segments per I/O command.
- QEMU SGL validation and a two-segment DMA read/write QTest.
- Linux `blk-mq` timeout handling that freezes submissions, fails ambiguous I/O, resets
  the controller, and recreates the admin-negotiated I/O queue.
- Linux capability-gated FUA propagation and block write-cache configuration.

## [0.3.0] - 2026-07-11

### Added

- Phase-tagged submission/completion queue engine with wraparound and backpressure.
- Initial QEMU PCI model with BAR0, DMA, MSI-X, reset, and RAM-backed block commands.
- Linux coherent admin rings, MSI-X setup, controller enable, and readiness handshake.
- Kernel module build and checkpatch validation.
- Cross-implementation ABI consistency tests and expanded project documentation.

### Changed

- Linux driver subtree moved to GPL-2.0-only for kernel API compatibility.
- Project documentation now distinguishes runnable components from unvalidated artifacts.

## [0.2.0] - 2026-07-11

### Added

- Live-page garbage collection, erase wear, write amplification, and ECC retry telemetry.
- JSONL trace replay.
- Host/controller ABI v0.1 and executable reference controller.
- Flush, FUA, discard, power-loss, and fault-injection behavior.

## [0.1.0] - 2026-07-11

### Added

- Discrete-event NAND channel/die simulator.
- Page-mapped FTL and FIFO/read-priority scheduling policies.
- CLI, tests, fio profiles, Linux PCI scaffold, architecture notes, and CI.

[0.3.0]: https://github.com/manishklach/openflash-controller-lab/releases/tag/v0.3.0
[0.2.0]: https://github.com/manishklach/openflash-controller-lab/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/manishklach/openflash-controller-lab/commits/main
